#include "obpch.h"
#include "Ouroboros/Renderer/Renderer.h"

#include "Ouroboros/Asset/GltfImporter.h"
#include "Ouroboros/Asset/Image.h"
#include "Ouroboros/Asset/MeshFactory.h"
#include "Ouroboros/Core/FileSystem.h"
#include "Ouroboros/Project/Project.h"
#include "Ouroboros/Renderer/GraphicsContext.h"
#include "Ouroboros/Renderer/ShaderTypes.h"

namespace Ouroboros {

	namespace {

		constexpr uint32_t RadianceSize = 512;
		constexpr uint32_t IrradianceSize = 32;
		constexpr uint32_t PrefilterSize = 128;
		constexpr uint32_t PrefilterMipCount = 6;
		constexpr uint32_t BrdfLutSize = 256;

		uint32_t DispatchCount(uint32_t size) { return (size + 7) / 8; }

		nvrhi::TextureSubresourceSet MipSlices(uint32_t mip, uint32_t mipCount = 1)
		{
			return nvrhi::TextureSubresourceSet(mip, mipCount, 0, 6);
		}

	}

	Renderer::Renderer(GraphicsContext& context)
		: m_Context(context), m_Device(context.GetDevice())
	{
		m_LinearClampSampler = m_Device->createSampler(nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
		m_LinearWrapSampler = m_Device->createSampler(nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Wrap).setMaxAnisotropy(8.0f));
		m_PointClampSampler = m_Device->createSampler(nvrhi::SamplerDesc().setAllFilters(false).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
		m_ShadowSampler = m_Device->createSampler(nvrhi::SamplerDesc()
			.setAllFilters(true)
			.setAllAddressModes(nvrhi::SamplerAddressMode::Border)
			.setBorderColor(nvrhi::Color(1.0f))
			.setReductionType(nvrhi::SamplerReductionType::Comparison));

		nvrhi::BindingLayoutDesc materialLayout;
		materialLayout.setVisibility(nvrhi::ShaderType::All)
			.setRegisterSpaceAndDescriptorSet(1) // descriptor set 1 of the forward pass
			.addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0))
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(2))
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(3))
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(4))
			.addItem(nvrhi::BindingLayoutItem::Sampler(0));
		m_MaterialLayout = m_Device->createBindingLayout(materialLayout);

		nvrhi::BindingLayoutDesc iblLayout;
		iblLayout.setVisibility(nvrhi::ShaderType::Compute)
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
			.addItem(nvrhi::BindingLayoutItem::Sampler(0))
			.addItem(nvrhi::BindingLayoutItem::Texture_UAV(0))
			.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(GPUIBLConstants)));
		m_IBLLayout = m_Device->createBindingLayout(iblLayout);

		nvrhi::BindingLayoutDesc brdfLayout;
		brdfLayout.setVisibility(nvrhi::ShaderType::Compute)
			.addItem(nvrhi::BindingLayoutItem::Texture_UAV(0))
			.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(GPUIBLConstants)));
		m_BrdfLayout = m_Device->createBindingLayout(brdfLayout);

		m_BlitLayout = m_Device->createBindingLayout(nvrhi::BindingLayoutDesc()
			.setVisibility(nvrhi::ShaderType::All)
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
			.addItem(nvrhi::BindingLayoutItem::Sampler(0)));

		auto createComputePipeline = [this](const char* shaderName, nvrhi::IBindingLayout* layout)
		{
			return m_Device->createComputePipeline(nvrhi::ComputePipelineDesc().setComputeShader(GetShader(shaderName)).addBindingLayout(layout));
		};
		m_EquirectPipeline = createComputePipeline("EquirectToCube.cs", m_IBLLayout);
		m_DownsamplePipeline = createComputePipeline("CubeDownsample.cs", m_IBLLayout);
		m_IrradiancePipeline = createComputePipeline("Irradiance.cs", m_IBLLayout);
		m_PrefilterPipeline = createComputePipeline("Prefilter.cs", m_IBLLayout);

		nvrhi::CommandListHandle commandList = m_Device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
		commandList->open();
		Image white{ 1, 1, { 255, 255, 255, 255 } };
		Image flatNormal{ 1, 1, { 128, 128, 255, 255 } };
		m_WhiteTexture = CreateTexture(commandList, white, false, "WhiteTexture");
		m_FlatNormalTexture = CreateTexture(commandList, flatNormal, false, "FlatNormalTexture");
		Submit(commandList);

		CreateBrdfLut();
		CreateFallbackEnvironment();
		m_Device->waitForIdle();

		OB_CORE_INFO("Renderer initialized");
	}

	Renderer::~Renderer()
	{
		m_Device->waitForIdle();
	}

	void Renderer::Submit(nvrhi::ICommandList* commandList)
	{
		commandList->close();
		m_Device->executeCommandList(commandList);
	}

	void Renderer::Blit(nvrhi::ICommandList* commandList, nvrhi::ITexture* source, nvrhi::IFramebuffer* framebuffer)
	{
		const nvrhi::FramebufferInfoEx& info = framebuffer->getFramebufferInfo();
		const uint32_t formatKey = info.colorFormats.empty() ? 0u : static_cast<uint32_t>(info.colorFormats[0]);

		nvrhi::GraphicsPipelineHandle& pipeline = m_BlitPipelines[formatKey];
		if (!pipeline)
		{
			nvrhi::RasterState raster;
			raster.setCullNone();
			nvrhi::DepthStencilState depth;
			depth.disableDepthTest().disableDepthWrite();

			nvrhi::GraphicsPipelineDesc desc;
			desc.setVertexShader(GetShader("Fullscreen.vs"))
				.setPixelShader(GetShader("Blit.ps"))
				.addBindingLayout(m_BlitLayout)
				.setRenderState(nvrhi::RenderState().setRasterState(raster).setDepthStencilState(depth));
			pipeline = m_Device->createGraphicsPipeline(desc, info);
		}

		nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(nvrhi::BindingSetDesc()
			.addItem(nvrhi::BindingSetItem::Texture_SRV(0, source))
			.addItem(nvrhi::BindingSetItem::Sampler(0, m_LinearClampSampler)), m_BlitLayout);

		nvrhi::GraphicsState state;
		state.setPipeline(pipeline)
			.setFramebuffer(framebuffer)
			.setViewport(nvrhi::ViewportState().addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(info.width), static_cast<float>(info.height))))
			.addBindingSet(bindingSet);
		commandList->setGraphicsState(state);
		commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
	}

	nvrhi::ShaderHandle Renderer::GetShader(const std::string& name)
	{
		if (auto it = m_Shaders.find(name); it != m_Shaders.end())
			return it->second;

		nvrhi::ShaderType type = nvrhi::ShaderType::None;
		if (name.ends_with(".vs"))
			type = nvrhi::ShaderType::Vertex;
		else if (name.ends_with(".ps"))
			type = nvrhi::ShaderType::Pixel;
		else if (name.ends_with(".cs"))
			type = nvrhi::ShaderType::Compute;
		OB_CORE_VERIFY(type != nvrhi::ShaderType::None, "Shader name '{}' must end with .vs, .ps or .cs", name);

		const std::filesystem::path path = FileSystem::GetEngineResourcesDirectory() / "Shaders" / (name + ".spv");
		std::optional<std::vector<uint8_t>> bytecode = FileSystem::ReadBinaryFile(path);
		if (!bytecode || bytecode->empty())
		{
			OB_CORE_CRITICAL("Missing shader '{}' (expected at '{}')", name, path.string());
			return nullptr;
		}

		nvrhi::ShaderHandle shader = m_Device->createShader(nvrhi::ShaderDesc().setShaderType(type).setEntryName("main").setDebugName(name),
			bytecode->data(), bytecode->size());
		m_Shaders[name] = shader;
		return shader;
	}

	Ref<Texture2D> Renderer::CreateTexture(nvrhi::ICommandList* commandList, const Image& image, bool srgb, const std::string& debugName)
	{
		const std::vector<Image> mips = ImageUtils::GenerateMips(image, srgb);

		nvrhi::TextureDesc desc;
		desc.setWidth(image.Width)
			.setHeight(image.Height)
			.setMipLevels(static_cast<uint32_t>(mips.size()))
			.setFormat(srgb ? nvrhi::Format::SRGBA8_UNORM : nvrhi::Format::RGBA8_UNORM)
			.setInitialState(nvrhi::ResourceStates::ShaderResource)
			.setKeepInitialState(true)
			.setDebugName(debugName);
		nvrhi::TextureHandle texture = m_Device->createTexture(desc);

		for (uint32_t mip = 0; mip < mips.size(); mip++)
			commandList->writeTexture(texture, 0, mip, mips[mip].Pixels.data(), static_cast<size_t>(mips[mip].Width) * 4);

		return CreateRef<Texture2D>(texture, image.Width, image.Height);
	}

	Ref<Texture2D> Renderer::GetTexture(nvrhi::ICommandList* commandList, const TextureSource& source, bool srgb)
	{
		if (!source.IsValid())
			return nullptr;

		const std::string key = source.GetCacheKey() + (srgb ? "|srgb" : "|linear");
		if (auto it = m_Textures.find(key); it != m_Textures.end())
			return it->second;
		if (m_FailedAssets.contains(key))
			return nullptr;

		std::string error;
		std::optional<Image> image = ImageUtils::Load(source, &error);
		if (!image)
		{
			OB_CORE_ERROR("Could not load texture '{}': {}", source.GetCacheKey(), error);
			m_FailedAssets.insert(key);
			return nullptr;
		}

		Ref<Texture2D> texture = CreateTexture(commandList, *image, srgb, source.GetCacheKey());
		m_Textures[key] = texture;
		return texture;
	}

	Ref<Material> Renderer::CreateMaterial(nvrhi::ICommandList* commandList, const MaterialData& data)
	{
		Ref<Material> material = CreateRef<Material>();
		material->Data = data;

		auto resolve = [&](const TextureSource& source, bool srgb, const Ref<Texture2D>& fallback)
		{
			Ref<Texture2D> texture = GetTexture(commandList, source, srgb);
			return texture ? texture : fallback;
		};
		Ref<Texture2D> baseColor = resolve(data.BaseColorTexture, true, m_WhiteTexture);
		Ref<Texture2D> metallicRoughness = resolve(data.MetallicRoughnessTexture, false, m_WhiteTexture);
		Ref<Texture2D> normal = resolve(data.NormalTexture, false, m_FlatNormalTexture);
		Ref<Texture2D> occlusion = resolve(data.OcclusionTexture, false, m_WhiteTexture);
		Ref<Texture2D> emissive = resolve(data.EmissiveTexture, true, m_WhiteTexture);
		material->Textures = { baseColor, metallicRoughness, normal, occlusion, emissive };

		GPUMaterialConstants constants{};
		constants.BaseColorFactor = data.BaseColorFactor;
		constants.EmissiveFactor = glm::vec4(data.EmissiveFactor, data.EmissiveStrength);
		constants.MetallicFactor = data.MetallicFactor;
		constants.RoughnessFactor = data.RoughnessFactor;
		constants.NormalScale = data.NormalScale;
		constants.OcclusionStrength = data.OcclusionTexture.IsValid() ? data.OcclusionStrength : 0.0f;

		material->ConstantBuffer = m_Device->createBuffer(nvrhi::BufferDesc()
			.setByteSize(sizeof(GPUMaterialConstants))
			.setIsConstantBuffer(true)
			.setInitialState(nvrhi::ResourceStates::ConstantBuffer)
			.setKeepInitialState(true)
			.setDebugName("Material:" + data.Name));
		commandList->writeBuffer(material->ConstantBuffer, &constants, sizeof(constants));

		nvrhi::BindingSetDesc setDesc;
		setDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, material->ConstantBuffer))
			.addItem(nvrhi::BindingSetItem::Texture_SRV(0, baseColor->GetHandle()))
			.addItem(nvrhi::BindingSetItem::Texture_SRV(1, metallicRoughness->GetHandle()))
			.addItem(nvrhi::BindingSetItem::Texture_SRV(2, normal->GetHandle()))
			.addItem(nvrhi::BindingSetItem::Texture_SRV(3, occlusion->GetHandle()))
			.addItem(nvrhi::BindingSetItem::Texture_SRV(4, emissive->GetHandle()))
			.addItem(nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler));
		material->BindingSet = m_Device->createBindingSet(setDesc, m_MaterialLayout);
		return material;
	}

	Ref<Mesh> Renderer::CreateMesh(const MeshData& data, const std::string& debugName)
	{
		std::string error;
		if (!data.Validate(&error))
		{
			OB_CORE_ERROR("Cannot create mesh '{}': {}", debugName, error);
			return nullptr;
		}

		Ref<Mesh> mesh = CreateRef<Mesh>();
		mesh->Submeshes = data.Submeshes;
		mesh->BoundsMin = data.BoundsMin;
		mesh->BoundsMax = data.BoundsMax;
		mesh->VertexCount = static_cast<uint32_t>(data.Vertices.size());
		mesh->IndexCount = static_cast<uint32_t>(data.Indices.size());

		const size_t vertexBytes = data.Vertices.size() * sizeof(Vertex);
		const size_t indexBytes = data.Indices.size() * sizeof(uint32_t);
		mesh->VertexBuffer = m_Device->createBuffer(nvrhi::BufferDesc()
			.setByteSize(vertexBytes)
			.setIsVertexBuffer(true)
			.setInitialState(nvrhi::ResourceStates::VertexBuffer)
			.setKeepInitialState(true)
			.setDebugName(debugName + ":Vertices"));
		mesh->IndexBuffer = m_Device->createBuffer(nvrhi::BufferDesc()
			.setByteSize(indexBytes)
			.setIsIndexBuffer(true)
			.setInitialState(nvrhi::ResourceStates::IndexBuffer)
			.setKeepInitialState(true)
			.setDebugName(debugName + ":Indices"));

		nvrhi::CommandListHandle commandList = m_Device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
		commandList->open();
		commandList->writeBuffer(mesh->VertexBuffer, data.Vertices.data(), vertexBytes);
		commandList->writeBuffer(mesh->IndexBuffer, data.Indices.data(), indexBytes);
		for (const MaterialData& materialData : data.Materials)
			mesh->Materials.push_back(CreateMaterial(commandList, materialData));
		Submit(commandList);
		return mesh;
	}

	Ref<Mesh> Renderer::GetMesh(const std::string& path)
	{
		if (path.empty())
			return nullptr;
		if (auto it = m_Meshes.find(path); it != m_Meshes.end())
			return it->second;
		if (m_FailedAssets.contains(path))
			return nullptr;

		std::optional<MeshData> data;
		if (MeshFactory::IsPrimitivePath(path))
		{
			data = MeshFactory::CreatePrimitive(path);
			if (!data)
				OB_CORE_ERROR("Unknown primitive mesh '{}'", path);
		}
		else
		{
			std::string error;
			data = GltfImporter::Import(Project::ResolveAssetPath(path), &error);
			if (!data)
				OB_CORE_ERROR("Could not import mesh '{}': {}", path, error);
		}

		Ref<Mesh> mesh = data ? CreateMesh(*data, path) : nullptr;
		if (!mesh)
		{
			m_FailedAssets.insert(path);
			return nullptr;
		}

		m_Meshes[path] = mesh;
		return mesh;
	}

	Ref<Environment> Renderer::GetEnvironment(const std::string& path)
	{
		if (path.empty())
			return nullptr;
		const std::string key = "env:" + path;
		if (auto it = m_Environments.find(key); it != m_Environments.end())
			return it->second;
		if (m_FailedAssets.contains(key))
			return nullptr;

		std::string error;
		std::optional<ImageHDR> image = ImageUtils::LoadHDR(Project::ResolveAssetPath(path), &error);
		if (!image)
		{
			OB_CORE_ERROR("Could not load environment '{}': {}", path, error);
			m_FailedAssets.insert(key);
			return nullptr;
		}

		Ref<Environment> environment = CreateEnvironment(*image, path);
		m_Environments[key] = environment;
		return environment;
	}

	void Renderer::ClearAssetCache()
	{
		m_Device->waitForIdle();
		m_Meshes.clear();
		m_Textures.clear();
		m_Environments.clear();
		m_FailedAssets.clear();
	}

	nvrhi::TextureHandle Renderer::CreateCubeTexture(uint32_t size, uint32_t mips, const char* name)
	{
		nvrhi::TextureDesc desc;
		desc.setWidth(size)
			.setHeight(size)
			.setArraySize(6)
			.setMipLevels(mips)
			.setDimension(nvrhi::TextureDimension::TextureCube)
			.setFormat(nvrhi::Format::RGBA16_FLOAT)
			.setIsUAV(true)
			.setInitialState(nvrhi::ResourceStates::ShaderResource)
			.setKeepInitialState(true)
			.setDebugName(name);
		return m_Device->createTexture(desc);
	}

	Ref<Environment> Renderer::CreateEnvironment(const ImageHDR& image, const std::string& debugName)
	{
		OB_CORE_INFO("Generating image based lighting for '{}' ({} x {})", debugName, image.Width, image.Height);

		nvrhi::TextureHandle equirect = m_Device->createTexture(nvrhi::TextureDesc()
			.setWidth(image.Width)
			.setHeight(image.Height)
			.setFormat(nvrhi::Format::RGBA32_FLOAT)
			.setInitialState(nvrhi::ResourceStates::ShaderResource)
			.setKeepInitialState(true)
			.setDebugName(debugName + ":Equirect"));

		Ref<Environment> environment = CreateRef<Environment>();
		const uint32_t radianceMips = ImageUtils::CalculateMipCount(RadianceSize, RadianceSize);
		environment->Radiance = CreateCubeTexture(RadianceSize, radianceMips, "EnvironmentRadiance");
		environment->Irradiance = CreateCubeTexture(IrradianceSize, 1, "EnvironmentIrradiance");
		environment->Prefiltered = CreateCubeTexture(PrefilterSize, PrefilterMipCount, "EnvironmentPrefiltered");
		environment->PrefilteredMipCount = PrefilterMipCount;

		nvrhi::CommandListHandle commandList = m_Device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
		commandList->open();
		commandList->writeTexture(equirect, 0, 0, image.Pixels.data(), static_cast<size_t>(image.Width) * 4 * sizeof(float));

		auto dispatch = [&](nvrhi::IComputePipeline* pipeline, nvrhi::ITexture* source, nvrhi::TextureSubresourceSet sourceSubresources,
			nvrhi::TextureDimension sourceDimension, nvrhi::ISampler* sampler, nvrhi::ITexture* output, uint32_t outputMip, const GPUIBLConstants& constants)
		{
			nvrhi::BindingSetDesc setDesc;
			setDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, source, nvrhi::Format::UNKNOWN, sourceSubresources, sourceDimension))
				.addItem(nvrhi::BindingSetItem::Sampler(0, sampler))
				.addItem(nvrhi::BindingSetItem::Texture_UAV(0, output, nvrhi::Format::UNKNOWN, MipSlices(outputMip), nvrhi::TextureDimension::Texture2DArray))
				.addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(GPUIBLConstants)));
			nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(setDesc, m_IBLLayout);

			commandList->setComputeState(nvrhi::ComputeState().setPipeline(pipeline).addBindingSet(bindingSet));
			commandList->setPushConstants(&constants, sizeof(constants));
			commandList->dispatch(DispatchCount(constants.OutputSize), DispatchCount(constants.OutputSize), 6);
		};

		// 1. Equirectangular -> radiance cube (mip 0)
		GPUIBLConstants constants{};
		constants.OutputSize = RadianceSize;
		dispatch(m_EquirectPipeline, equirect, nvrhi::AllSubresources, nvrhi::TextureDimension::Texture2D, m_LinearWrapSampler, environment->Radiance, 0, constants);

		// 2. Radiance mip chain
		for (uint32_t mip = 1; mip < radianceMips; mip++)
		{
			constants.OutputSize = std::max(1u, RadianceSize >> mip);
			constants.MipLevel = mip;
			dispatch(m_DownsamplePipeline, environment->Radiance, MipSlices(mip - 1), nvrhi::TextureDimension::TextureCube, m_LinearClampSampler,
				environment->Radiance, mip, constants);
		}

		// 3. Diffuse irradiance
		constants = GPUIBLConstants{};
		constants.OutputSize = IrradianceSize;
		constants.SourceSize = static_cast<float>(RadianceSize);
		dispatch(m_IrradiancePipeline, environment->Radiance, MipSlices(0, radianceMips), nvrhi::TextureDimension::TextureCube, m_LinearClampSampler,
			environment->Irradiance, 0, constants);

		// 4. Specular pre-filtering, one roughness per mip
		for (uint32_t mip = 0; mip < PrefilterMipCount; mip++)
		{
			constants = GPUIBLConstants{};
			constants.OutputSize = std::max(1u, PrefilterSize >> mip);
			constants.MipLevel = mip;
			constants.Roughness = static_cast<float>(mip) / static_cast<float>(PrefilterMipCount - 1);
			constants.SampleCount = mip == 0 ? 1 : 256;
			constants.SourceSize = static_cast<float>(RadianceSize);
			dispatch(m_PrefilterPipeline, environment->Radiance, MipSlices(0, radianceMips), nvrhi::TextureDimension::TextureCube, m_LinearClampSampler,
				environment->Prefiltered, mip, constants);
		}

		Submit(commandList);
		m_Device->waitForIdle();
		return environment;
	}

	void Renderer::CreateBrdfLut()
	{
		m_BrdfLut = m_Device->createTexture(nvrhi::TextureDesc()
			.setWidth(BrdfLutSize)
			.setHeight(BrdfLutSize)
			.setFormat(nvrhi::Format::RG16_FLOAT)
			.setIsUAV(true)
			.setInitialState(nvrhi::ResourceStates::ShaderResource)
			.setKeepInitialState(true)
			.setDebugName("BrdfLut"));

		nvrhi::ComputePipelineHandle pipeline = m_Device->createComputePipeline(nvrhi::ComputePipelineDesc()
			.setComputeShader(GetShader("BrdfLut.cs"))
			.addBindingLayout(m_BrdfLayout));

		nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(nvrhi::BindingSetDesc()
			.addItem(nvrhi::BindingSetItem::Texture_UAV(0, m_BrdfLut))
			.addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(GPUIBLConstants))), m_BrdfLayout);

		GPUIBLConstants constants{};
		constants.OutputSize = BrdfLutSize;

		nvrhi::CommandListHandle commandList = m_Device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
		commandList->open();
		commandList->setComputeState(nvrhi::ComputeState().setPipeline(pipeline).addBindingSet(bindingSet));
		commandList->setPushConstants(&constants, sizeof(constants));
		commandList->dispatch(DispatchCount(BrdfLutSize), DispatchCount(BrdfLutSize), 1);
		Submit(commandList);
	}

	void Renderer::CreateFallbackEnvironment()
	{
		// Bound when a scene has no sky light; the shaders use the ambient color instead of sampling it.
		m_FallbackEnvironment = CreateRef<Environment>();
		m_FallbackEnvironment->Radiance = CreateCubeTexture(1, 1, "FallbackRadiance");
		m_FallbackEnvironment->Irradiance = CreateCubeTexture(1, 1, "FallbackIrradiance");
		m_FallbackEnvironment->Prefiltered = CreateCubeTexture(1, 1, "FallbackPrefiltered");
		m_FallbackEnvironment->PrefilteredMipCount = 1;

		nvrhi::CommandListHandle commandList = m_Device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
		commandList->open();
		for (nvrhi::ITexture* texture : { m_FallbackEnvironment->Radiance.Get(), m_FallbackEnvironment->Irradiance.Get(), m_FallbackEnvironment->Prefiltered.Get() })
			commandList->clearTextureFloat(texture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
		Submit(commandList);
	}

}
