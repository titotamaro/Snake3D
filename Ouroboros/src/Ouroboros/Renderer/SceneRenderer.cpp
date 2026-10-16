#include "obpch.h"
#include "Ouroboros/Renderer/SceneRenderer.h"

#include "Ouroboros/Math/Math.h"
#include "Ouroboros/Renderer/Renderer.h"
#include "Ouroboros/Scene/Entity.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Ouroboros {

	namespace {

		constexpr nvrhi::Format DepthFormat = nvrhi::Format::D32;
		constexpr nvrhi::Format HDRFormat = nvrhi::Format::RGBA16_FLOAT;
		constexpr nvrhi::Format NormalFormat = nvrhi::Format::RGBA16_FLOAT;
		constexpr nvrhi::Format EntityIDFormat = nvrhi::Format::R32_UINT;
		constexpr nvrhi::Format AOFormat = nvrhi::Format::R8_UNORM;
		constexpr nvrhi::Format LDRFormat = nvrhi::Format::RGBA8_UNORM;
		constexpr uint32_t NoEntity = 0xFFFFFFFFu;

		nvrhi::TextureHandle CreateRenderTarget(nvrhi::IDevice* device, uint32_t width, uint32_t height, nvrhi::Format format, const char* name,
			nvrhi::Color clearValue = nvrhi::Color(0.0f))
		{
			const bool isDepth = format == DepthFormat;
			nvrhi::TextureDesc desc;
			desc.setWidth(width)
				.setHeight(height)
				.setFormat(format)
				.setIsRenderTarget(true)
				.setClearValue(clearValue)
				.setInitialState(isDepth ? nvrhi::ResourceStates::DepthWrite : nvrhi::ResourceStates::RenderTarget)
				.setKeepInitialState(true)
				.setDebugName(name);
			return device->createTexture(desc);
		}

	}

	SceneRenderer::SceneRenderer(Renderer& renderer)
		: m_Renderer(renderer), m_Device(renderer.GetDevice())
	{
		m_FrameConstants = m_Device->createBuffer(nvrhi::BufferDesc()
			.setByteSize(sizeof(GPUFrameConstants))
			.setIsConstantBuffer(true)
			.setIsVolatile(true)
			.setMaxVersions(64)
			.setDebugName("FrameConstants"));

		m_ShadowMap = m_Device->createTexture(nvrhi::TextureDesc()
			.setWidth(ShadowMapSize)
			.setHeight(ShadowMapSize)
			.setArraySize(ShadowCascadeCount)
			.setDimension(nvrhi::TextureDimension::Texture2DArray)
			.setFormat(DepthFormat)
			.setIsRenderTarget(true)
			.setClearValue(nvrhi::Color(1.0f))
			.setInitialState(nvrhi::ResourceStates::ShaderResource)
			.setKeepInitialState(true)
			.setDebugName("ShadowMap"));
		for (uint32_t cascade = 0; cascade < ShadowCascadeCount; cascade++)
		{
			m_ShadowFramebuffers[cascade] = m_Device->createFramebuffer(nvrhi::FramebufferDesc()
				.setDepthAttachment(m_ShadowMap, nvrhi::TextureSubresourceSet(0, 1, cascade, 1)));
		}

		CreatePipelines();
		SetViewportSize(1280, 720);
	}

	SceneRenderer::~SceneRenderer() = default;

	void SceneRenderer::CreatePipelines()
	{
		const nvrhi::VertexAttributeDesc attributes[] = {
			nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, Position)).setElementStride(sizeof(Vertex)),
			nvrhi::VertexAttributeDesc().setName("NORMAL").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, Normal)).setElementStride(sizeof(Vertex)),
			nvrhi::VertexAttributeDesc().setName("TANGENT").setFormat(nvrhi::Format::RGBA32_FLOAT).setOffset(offsetof(Vertex, Tangent)).setElementStride(sizeof(Vertex)),
			nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(Vertex, TexCoord)).setElementStride(sizeof(Vertex))
		};
		nvrhi::ShaderHandle meshVS = m_Renderer.GetShader("Mesh.vs");
		m_InputLayout = m_Device->createInputLayout(attributes, static_cast<uint32_t>(std::size(attributes)), meshVS);
		nvrhi::ShaderHandle shadowVS = m_Renderer.GetShader("Shadow.vs");
		m_ShadowInputLayout = m_Device->createInputLayout(attributes, 1, shadowVS);

		// Binding layouts ------------------------------------------------------------------------------------------
		m_DepthPassLayout = m_Device->createBindingLayout(nvrhi::BindingLayoutDesc()
			.setVisibility(nvrhi::ShaderType::All)
			.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
			.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0))
			.addItem(nvrhi::BindingLayoutItem::PushConstants(1, sizeof(GPUPushConstants))));

		m_ForwardLayout = m_Device->createBindingLayout(nvrhi::BindingLayoutDesc()
			.setVisibility(nvrhi::ShaderType::All)
			.setRegisterSpaceAndDescriptorSet(0) // set 1 is the material layout
			.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
			.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0))
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(2))
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(3))
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(4))
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(5))
			.addItem(nvrhi::BindingLayoutItem::Sampler(0))
			.addItem(nvrhi::BindingLayoutItem::Sampler(1))
			.addItem(nvrhi::BindingLayoutItem::Sampler(2))
			.addItem(nvrhi::BindingLayoutItem::PushConstants(1, sizeof(GPUPushConstants))));

		m_FrameOnlyLayout = m_Device->createBindingLayout(nvrhi::BindingLayoutDesc()
			.setVisibility(nvrhi::ShaderType::All)
			.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0)));

		m_SkyLayout = m_Device->createBindingLayout(nvrhi::BindingLayoutDesc()
			.setVisibility(nvrhi::ShaderType::All)
			.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
			.addItem(nvrhi::BindingLayoutItem::Sampler(0)));

		m_SSAOLayout = m_Device->createBindingLayout(nvrhi::BindingLayoutDesc()
			.setVisibility(nvrhi::ShaderType::All)
			.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
			.addItem(nvrhi::BindingLayoutItem::Sampler(0)));

		m_BlurLayout = m_Device->createBindingLayout(nvrhi::BindingLayoutDesc()
			.setVisibility(nvrhi::ShaderType::All)
			.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
			.addItem(nvrhi::BindingLayoutItem::Sampler(0))
			.addItem(nvrhi::BindingLayoutItem::PushConstants(1, sizeof(glm::vec4))));

		m_TonemapLayout = m_Device->createBindingLayout(nvrhi::BindingLayoutDesc()
			.setVisibility(nvrhi::ShaderType::All)
			.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
			.addItem(nvrhi::BindingLayoutItem::Sampler(0)));

		// Pipelines ------------------------------------------------------------------------------------------------
		nvrhi::RasterState meshRaster;
		meshRaster.setCullBack().setFrontCounterClockwise(true);

		nvrhi::RasterState noCull;
		noCull.setCullNone();

		nvrhi::DepthStencilState depthWrite;
		depthWrite.enableDepthTest().enableDepthWrite().setDepthFunc(nvrhi::ComparisonFunc::Less);
		nvrhi::DepthStencilState depthEqual;
		depthEqual.enableDepthTest().disableDepthWrite().setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);
		nvrhi::DepthStencilState noDepth;
		noDepth.disableDepthTest().disableDepthWrite();

		{
			nvrhi::RasterState shadowRaster;
			shadowRaster.setCullNone().setFrontCounterClockwise(true).setDepthClipEnable(false);
			shadowRaster.slopeScaledDepthBias = 1.5f;

			nvrhi::GraphicsPipelineDesc desc;
			desc.setInputLayout(m_ShadowInputLayout)
				.setVertexShader(shadowVS)
				.addBindingLayout(m_DepthPassLayout)
				.setRenderState(nvrhi::RenderState().setRasterState(shadowRaster).setDepthStencilState(depthWrite));
			m_ShadowPipeline = m_Device->createGraphicsPipeline(desc, nvrhi::FramebufferInfo().setDepthFormat(DepthFormat));
		}
		{
			nvrhi::GraphicsPipelineDesc desc;
			desc.setInputLayout(m_InputLayout)
				.setVertexShader(meshVS)
				.setPixelShader(m_Renderer.GetShader("PrePass.ps"))
				.addBindingLayout(m_DepthPassLayout)
				.setRenderState(nvrhi::RenderState().setRasterState(meshRaster).setDepthStencilState(depthWrite));
			m_PrePassPipeline = m_Device->createGraphicsPipeline(desc,
				nvrhi::FramebufferInfo().addColorFormat(NormalFormat).addColorFormat(EntityIDFormat).setDepthFormat(DepthFormat));
		}
		{
			nvrhi::GraphicsPipelineDesc desc;
			desc.setInputLayout(m_InputLayout)
				.setVertexShader(meshVS)
				.setPixelShader(m_Renderer.GetShader("PBR.ps"))
				.addBindingLayout(m_ForwardLayout)
				.addBindingLayout(m_Renderer.GetMaterialLayout())
				.setRenderState(nvrhi::RenderState().setRasterState(meshRaster).setDepthStencilState(depthEqual));
			m_ForwardPipeline = m_Device->createGraphicsPipeline(desc, nvrhi::FramebufferInfo().addColorFormat(HDRFormat).setDepthFormat(DepthFormat));
		}

		nvrhi::ShaderHandle fullscreenVS = m_Renderer.GetShader("Fullscreen.vs");
		auto createFullscreenPipeline = [&](const char* pixelShader, nvrhi::IBindingLayout* layout, const nvrhi::DepthStencilState& depthState,
			const nvrhi::BlendState& blendState, const nvrhi::FramebufferInfo& framebufferInfo)
		{
			nvrhi::GraphicsPipelineDesc desc;
			desc.setVertexShader(fullscreenVS)
				.setPixelShader(m_Renderer.GetShader(pixelShader))
				.addBindingLayout(layout)
				.setRenderState(nvrhi::RenderState().setRasterState(noCull).setDepthStencilState(depthState).setBlendState(blendState));
			return m_Device->createGraphicsPipeline(desc, framebufferInfo);
		};

		nvrhi::BlendState opaque;
		nvrhi::BlendState alphaBlend;
		alphaBlend.targets[0].enableBlend()
			.setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
			.setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
			.setSrcBlendAlpha(nvrhi::BlendFactor::One)
			.setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);

		const nvrhi::FramebufferInfo hdrInfo = nvrhi::FramebufferInfo().addColorFormat(HDRFormat).setDepthFormat(DepthFormat);
		m_SkyPipeline = createFullscreenPipeline("Skybox.ps", m_SkyLayout, depthEqual, opaque, hdrInfo);
		m_GridPipeline = createFullscreenPipeline("Grid.ps", m_FrameOnlyLayout, depthEqual, alphaBlend, hdrInfo);
		m_SSAOPipeline = createFullscreenPipeline("SSAO.ps", m_SSAOLayout, noDepth, opaque, nvrhi::FramebufferInfo().addColorFormat(AOFormat));
		m_BlurPipeline = createFullscreenPipeline("SSAOBlur.ps", m_BlurLayout, noDepth, opaque, nvrhi::FramebufferInfo().addColorFormat(AOFormat));
		m_TonemapPipeline = createFullscreenPipeline("Tonemap.ps", m_TonemapLayout, noDepth, opaque, nvrhi::FramebufferInfo().addColorFormat(LDRFormat));
	}

	void SceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
	{
		width = std::max(width, 1u);
		height = std::max(height, 1u);
		if (width == m_Width && height == m_Height)
			return;

		m_Width = width;
		m_Height = height;
		CreateTargets();
	}

	void SceneRenderer::CreateTargets()
	{
		m_Device->waitForIdle();

		m_Depth = CreateRenderTarget(m_Device, m_Width, m_Height, DepthFormat, "SceneDepth", nvrhi::Color(1.0f));
		m_Normals = CreateRenderTarget(m_Device, m_Width, m_Height, NormalFormat, "SceneNormals");
		m_EntityIDs = CreateRenderTarget(m_Device, m_Width, m_Height, EntityIDFormat, "SceneEntityIDs");
		m_AORaw = CreateRenderTarget(m_Device, m_Width, m_Height, AOFormat, "SceneAORaw", nvrhi::Color(1.0f));
		m_AOTemp = CreateRenderTarget(m_Device, m_Width, m_Height, AOFormat, "SceneAOTemp", nvrhi::Color(1.0f));
		m_AO = CreateRenderTarget(m_Device, m_Width, m_Height, AOFormat, "SceneAO", nvrhi::Color(1.0f));
		m_HDR = CreateRenderTarget(m_Device, m_Width, m_Height, HDRFormat, "SceneHDR");
		m_LDR = CreateRenderTarget(m_Device, m_Width, m_Height, LDRFormat, "SceneLDR");

		m_PrePassFramebuffer = m_Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_Normals).addColorAttachment(m_EntityIDs).setDepthAttachment(m_Depth));
		m_AORawFramebuffer = m_Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_AORaw));
		m_AOTempFramebuffer = m_Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_AOTemp));
		m_AOFramebuffer = m_Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_AO));
		m_HDRFramebuffer = m_Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_HDR).setDepthAttachment(m_Depth));
		m_LDRFramebuffer = m_Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_LDR));

		nvrhi::ISampler* pointClamp = m_Renderer.GetPointClampSampler();

		m_FrameOnlySet = m_Device->createBindingSet(nvrhi::BindingSetDesc()
			.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_FrameConstants)), m_FrameOnlyLayout);

		m_SSAOSet = m_Device->createBindingSet(nvrhi::BindingSetDesc()
			.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_FrameConstants))
			.addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_Depth))
			.addItem(nvrhi::BindingSetItem::Texture_SRV(1, m_Normals))
			.addItem(nvrhi::BindingSetItem::Sampler(0, pointClamp)), m_SSAOLayout);

		auto createBlurSet = [&](nvrhi::ITexture* source)
		{
			return m_Device->createBindingSet(nvrhi::BindingSetDesc()
				.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_FrameConstants))
				.addItem(nvrhi::BindingSetItem::Texture_SRV(0, source))
				.addItem(nvrhi::BindingSetItem::Texture_SRV(1, m_Depth))
				.addItem(nvrhi::BindingSetItem::Sampler(0, pointClamp))
				.addItem(nvrhi::BindingSetItem::PushConstants(1, sizeof(glm::vec4))), m_BlurLayout);
		};
		m_BlurHorizontalSet = createBlurSet(m_AORaw);
		m_BlurVerticalSet = createBlurSet(m_AOTemp);

		m_TonemapSet = m_Device->createBindingSet(nvrhi::BindingSetDesc()
			.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_FrameConstants))
			.addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_HDR))
			.addItem(nvrhi::BindingSetItem::Texture_SRV(1, m_EntityIDs))
			.addItem(nvrhi::BindingSetItem::Sampler(0, pointClamp)), m_TonemapLayout);

		// Sets referencing the draw buffer / environment are rebuilt on demand.
		m_DepthPassSet = nullptr;
		m_ForwardSet = nullptr;
		m_SkySet = nullptr;
		m_BoundEnvironment = nullptr;
	}

	void SceneRenderer::CreatePassBindingSets(nvrhi::ITexture* environmentTextures[3])
	{
		m_DepthPassSet = m_Device->createBindingSet(nvrhi::BindingSetDesc()
			.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_FrameConstants))
			.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_DrawBuffer))
			.addItem(nvrhi::BindingSetItem::PushConstants(1, sizeof(GPUPushConstants))), m_DepthPassLayout);

		m_ForwardSet = m_Device->createBindingSet(nvrhi::BindingSetDesc()
			.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_FrameConstants))
			.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_DrawBuffer))
			.addItem(nvrhi::BindingSetItem::Texture_SRV(1, m_ShadowMap, nvrhi::Format::UNKNOWN, nvrhi::AllSubresources, nvrhi::TextureDimension::Texture2DArray))
			.addItem(nvrhi::BindingSetItem::Texture_SRV(2, environmentTextures[1], nvrhi::Format::UNKNOWN, nvrhi::AllSubresources, nvrhi::TextureDimension::TextureCube))
			.addItem(nvrhi::BindingSetItem::Texture_SRV(3, environmentTextures[2], nvrhi::Format::UNKNOWN, nvrhi::AllSubresources, nvrhi::TextureDimension::TextureCube))
			.addItem(nvrhi::BindingSetItem::Texture_SRV(4, m_Renderer.GetBrdfLut()))
			.addItem(nvrhi::BindingSetItem::Texture_SRV(5, m_AO))
			.addItem(nvrhi::BindingSetItem::Sampler(0, m_Renderer.GetLinearClampSampler()))
			.addItem(nvrhi::BindingSetItem::Sampler(1, m_Renderer.GetShadowSampler()))
			.addItem(nvrhi::BindingSetItem::Sampler(2, m_Renderer.GetPointClampSampler()))
			.addItem(nvrhi::BindingSetItem::PushConstants(1, sizeof(GPUPushConstants))), m_ForwardLayout);

		m_SkySet = m_Device->createBindingSet(nvrhi::BindingSetDesc()
			.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_FrameConstants))
			.addItem(nvrhi::BindingSetItem::Texture_SRV(0, environmentTextures[0], nvrhi::Format::UNKNOWN, nvrhi::AllSubresources, nvrhi::TextureDimension::TextureCube))
			.addItem(nvrhi::BindingSetItem::Sampler(0, m_Renderer.GetLinearClampSampler())), m_SkyLayout);
	}

	std::optional<RenderCamera> SceneRenderer::GetSceneCamera(Scene& scene) const
	{
		Entity cameraEntity = scene.GetPrimaryCameraEntity();
		if (!cameraEntity)
			return std::nullopt;

		const CameraComponent& camera = cameraEntity.GetComponent<CameraComponent>();
		const TransformComponent& transform = cameraEntity.GetTransform();
		const float aspect = static_cast<float>(m_Width) / static_cast<float>(std::max(m_Height, 1u));

		RenderCamera result;
		result.View = glm::inverse(Math::ComposeTransform(transform.Translation, transform.Rotation, glm::vec3(1.0f)));
		result.Projection = camera.GetProjection(aspect);
		result.Position = transform.Translation;
		result.Near = camera.GetNear();
		result.Far = camera.GetFar();
		result.Exposure = camera.Exposure;
		return result;
	}

	void SceneRenderer::ComputeShadowCascades(const RenderCamera& camera, const glm::vec3& lightDirection, float shadowDistance, GPUFrameConstants& constants) const
	{
		const float nearClip = std::max(camera.Near, 0.01f);
		const float farClip = std::max(std::min(camera.Far, shadowDistance), nearClip + 1.0f);

		// Frustum corners at the near and far clip planes (world space).
		const glm::mat4 inverseViewProjection = glm::inverse(camera.Projection * camera.View);
		glm::vec3 nearCorners[4], farCorners[4];
		const glm::vec2 ndc[4] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
		for (int i = 0; i < 4; i++)
		{
			glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndc[i], 0.0f, 1.0f);
			glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndc[i], 1.0f, 1.0f);
			nearCorners[i] = glm::vec3(nearPoint) / nearPoint.w;
			farCorners[i] = glm::vec3(farPoint) / farPoint.w;
		}
		const float cameraNear = camera.Near;
		const float cameraFar = camera.Far;

		// Practical split scheme (blend of logarithmic and uniform splits).
		constexpr float lambda = 0.5f;
		float splits[ShadowCascadeCount];
		for (uint32_t i = 0; i < ShadowCascadeCount; i++)
		{
			const float p = static_cast<float>(i + 1) / static_cast<float>(ShadowCascadeCount);
			const float logSplit = nearClip * std::pow(farClip / nearClip, p);
			const float uniformSplit = nearClip + (farClip - nearClip) * p;
			splits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
		}

		const glm::vec3 up = std::abs(lightDirection.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
		float sliceStart = nearClip;
		for (uint32_t cascade = 0; cascade < ShadowCascadeCount; cascade++)
		{
			const float sliceEnd = splits[cascade];
			auto cornerAt = [&](int i, float distance)
			{
				const float t = (distance - cameraNear) / (cameraFar - cameraNear);
				return glm::mix(nearCorners[i], farCorners[i], t);
			};

			glm::vec3 corners[8];
			for (int i = 0; i < 4; i++)
			{
				corners[i] = cornerAt(i, sliceStart);
				corners[i + 4] = cornerAt(i, sliceEnd);
			}

			glm::vec3 center(0.0f);
			for (const glm::vec3& corner : corners)
				center += corner;
			center /= 8.0f;

			float radius = 0.0f;
			for (const glm::vec3& corner : corners)
				radius = std::max(radius, glm::length(corner - center));
			radius = std::ceil(radius * 16.0f) / 16.0f;

			constexpr float casterMargin = 50.0f; // include casters behind the slice (towards the light)
			const glm::vec3 eye = center - lightDirection * (radius + casterMargin);
			const glm::mat4 lightView = glm::lookAtRH(eye, center, up);
			glm::mat4 lightProjection = Math::OrthographicProjection(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + casterMargin);

			// Snap to shadow map texels to avoid shimmering when the camera moves.
			const glm::mat4 shadowMatrix = lightProjection * lightView;
			glm::vec4 origin = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
			origin *= static_cast<float>(ShadowMapSize) * 0.5f;
			const glm::vec4 rounded = glm::round(origin);
			glm::vec4 offset = (rounded - origin) * (2.0f / static_cast<float>(ShadowMapSize));
			lightProjection[3][0] += offset.x;
			lightProjection[3][1] += offset.y;

			constants.CascadeViewProjection[cascade] = lightProjection * lightView;
			constants.CascadeSplits[cascade] = sliceEnd;
			sliceStart = sliceEnd;
		}
	}

	void SceneRenderer::Render(Scene& scene, const RenderCamera& camera, nvrhi::ICommandList* commandList)
	{
		m_Stats = SceneRendererStats();
		entt::registry& registry = scene.GetRegistry();

		// Gather draws ---------------------------------------------------------------------------------------------
		m_DrawItems.clear();
		m_DrawData.clear();
		for (UUID uuid : scene.GetEntityOrder())
		{
			Entity entity = scene.GetEntityByUUID(uuid);
			const MeshRendererComponent* meshRenderer = entity.TryGetComponent<MeshRendererComponent>();
			if (!meshRenderer || !meshRenderer->Visible)
				continue;

			Ref<Mesh> mesh = m_Renderer.GetMesh(meshRenderer->Mesh);
			if (!mesh)
				continue;

			const glm::mat4 model = entity.GetTransform().GetTransform();
			GPUDrawData data{};
			data.Model = model;
			data.NormalMatrix = glm::mat4(Math::NormalMatrix(model));
			data.Color = meshRenderer->Color;
			data.Emissive = glm::vec4(meshRenderer->Emissive, meshRenderer->EmissiveIntensity);
			data.Metallic = meshRenderer->Metallic;
			data.Roughness = meshRenderer->Roughness;
			data.EntityID = static_cast<uint32_t>(entity.GetHandle()) + 1;

			m_DrawItems.push_back({ mesh, static_cast<uint32_t>(m_DrawData.size()), meshRenderer->CastShadows });
			m_DrawData.push_back(data);
		}
		m_Stats.Meshes = static_cast<uint32_t>(m_DrawItems.size());

		// Draw buffer (grows on demand; binding sets referencing it are rebuilt)
		const uint32_t requiredCapacity = std::max<uint32_t>(64, static_cast<uint32_t>(m_DrawData.size()));
		if (!m_DrawBuffer || requiredCapacity > m_DrawBufferCapacity)
		{
			m_DrawBufferCapacity = std::max(requiredCapacity, m_DrawBufferCapacity * 2);
			m_DrawBuffer = m_Device->createBuffer(nvrhi::BufferDesc()
				.setByteSize(static_cast<uint64_t>(m_DrawBufferCapacity) * sizeof(GPUDrawData))
				.setStructStride(sizeof(GPUDrawData))
				.setInitialState(nvrhi::ResourceStates::ShaderResource)
				.setKeepInitialState(true)
				.setDebugName("DrawData"));
			m_DepthPassSet = nullptr;
		}

		// Lights and environment -----------------------------------------------------------------------------------
		GPUFrameConstants constants{};
		constants.View = camera.View;
		constants.Projection = camera.Projection;
		constants.ViewProjection = camera.Projection * camera.View;
		constants.InverseView = glm::inverse(camera.View);
		constants.InverseProjection = glm::inverse(camera.Projection);
		constants.InverseViewProjection = glm::inverse(constants.ViewProjection);
		constants.CameraPosition = glm::vec4(camera.Position, camera.Exposure);
		constants.ViewportSize = glm::vec4(static_cast<float>(m_Width), static_cast<float>(m_Height), 1.0f / static_cast<float>(m_Width), 1.0f / static_cast<float>(m_Height));
		constants.AOParams = glm::vec4(m_Settings.SSAORadius, m_Settings.SSAOIntensity, 0.02f, m_Settings.EnableSSAO ? 1.0f : 0.0f);
		constants.AmbientColor = glm::vec4(m_Settings.AmbientColor, 1.0f);
		constants.GridParams = glm::vec4(m_Settings.ShowGrid ? 1.0f : 0.0f, 1.0f, 150.0f, 0.0f);
		constants.ShadowParams = glm::vec4(1.0f, 1.0f / static_cast<float>(ShadowMapSize), m_Settings.ShadowDepthBias, m_Settings.ShadowNormalBias);
		for (uint32_t i = 0; i < ShadowCascadeCount; i++)
			constants.CascadeSplits[i] = -1.0f;

		bool shadowsEnabled = false;
		// One directional light is supported: the first one found.
		if (auto lights = registry.view<DirectionalLightComponent>(); lights.begin() != lights.end())
		{
			Entity entity(*lights.begin(), &scene);
			const DirectionalLightComponent& light = entity.GetComponent<DirectionalLightComponent>();
			const glm::vec3 direction = glm::normalize(entity.GetTransform().GetForward());
			constants.LightDirection = glm::vec4(direction, 1.0f);
			shadowsEnabled = light.CastShadows && m_Settings.EnableShadows;
			constants.LightColor = glm::vec4(light.Color * light.Intensity, shadowsEnabled ? 1.0f : 0.0f);
			constants.ShadowParams.x = light.ShadowSoftness;
			if (shadowsEnabled)
				ComputeShadowCascades(camera, direction, light.ShadowDistance, constants);
		}

		uint32_t pointLightCount = 0;
		for (entt::entity handle : registry.view<PointLightComponent>())
		{
			if (pointLightCount >= MaxPointLights)
				break;
			Entity entity(handle, &scene);
			const PointLightComponent& light = entity.GetComponent<PointLightComponent>();
			constants.PointLights[pointLightCount++] = { glm::vec4(entity.GetTransform().Translation, light.Radius), glm::vec4(light.Color, light.Intensity) };
		}

		Ref<Environment> environment;
		if (auto skyLights = registry.view<SkyLightComponent>(); skyLights.begin() != skyLights.end())
		{
			const SkyLightComponent& skyLight = registry.get<SkyLightComponent>(*skyLights.begin());
			environment = m_Renderer.GetEnvironment(skyLight.Environment);
			if (environment)
				constants.EnvironmentParams = glm::vec4(skyLight.Intensity, skyLight.Rotation, static_cast<float>(environment->PrefilteredMipCount), 1.0f);
			constants.GridParams.w = skyLight.ShowBackground ? 1.0f : 0.0f;
		}
		if (!environment)
			environment = m_Renderer.GetFallbackEnvironment();

		constants.Counts = glm::uvec4(pointLightCount, m_SelectedEntity == NoEntity ? 0u : m_SelectedEntity + 1, 0u, static_cast<uint32_t>(m_Settings.View));

		if (!m_DepthPassSet || m_BoundEnvironment != environment->Radiance.Get())
		{
			nvrhi::ITexture* environmentTextures[3] = { environment->Radiance, environment->Irradiance, environment->Prefiltered };
			CreatePassBindingSets(environmentTextures);
			m_BoundEnvironment = environment->Radiance;
		}

		commandList->beginMarker("SceneRenderer");
		commandList->writeBuffer(m_FrameConstants, &constants, sizeof(constants));
		if (!m_DrawData.empty())
			commandList->writeBuffer(m_DrawBuffer, m_DrawData.data(), m_DrawData.size() * sizeof(GPUDrawData));

		uint32_t viewportSize[2] = { m_Width, m_Height };

		// Shadows ---------------------------------------------------------------------------------------------------
		commandList->beginMarker("Shadows");
		commandList->clearDepthStencilTexture(m_ShadowMap, nvrhi::AllSubresources, true, 1.0f, false, 0);
		if (shadowsEnabled)
		{
			uint32_t shadowSize[2] = { ShadowMapSize, ShadowMapSize };
			for (uint32_t cascade = 0; cascade < ShadowCascadeCount; cascade++)
				DrawMeshes(commandList, m_ShadowPipeline, m_ShadowFramebuffers[cascade], m_DepthPassSet, false, true, cascade, shadowSize);
		}
		commandList->endMarker();

		// Pre-pass ----------------------------------------------------------------------------------------------------
		commandList->beginMarker("PrePass");
		commandList->clearTextureFloat(m_Normals, nvrhi::AllSubresources, nvrhi::Color(0.0f));
		commandList->clearTextureUInt(m_EntityIDs, nvrhi::AllSubresources, 0);
		commandList->clearDepthStencilTexture(m_Depth, nvrhi::AllSubresources, true, 1.0f, false, 0);
		DrawMeshes(commandList, m_PrePassPipeline, m_PrePassFramebuffer, m_DepthPassSet, false, false, 0, viewportSize);
		commandList->endMarker();

		// SSAO --------------------------------------------------------------------------------------------------------
		commandList->beginMarker("SSAO");
		if (m_Settings.EnableSSAO)
		{
			DrawFullscreen(commandList, m_SSAOPipeline, m_AORawFramebuffer, m_SSAOSet);
			const glm::vec4 horizontal(1.0f, 0.0f, 0.0f, 0.0f);
			const glm::vec4 vertical(0.0f, 1.0f, 0.0f, 0.0f);
			DrawFullscreen(commandList, m_BlurPipeline, m_AOTempFramebuffer, m_BlurHorizontalSet, &horizontal, sizeof(horizontal));
			DrawFullscreen(commandList, m_BlurPipeline, m_AOFramebuffer, m_BlurVerticalSet, &vertical, sizeof(vertical));
		}
		else
		{
			commandList->clearTextureFloat(m_AO, nvrhi::AllSubresources, nvrhi::Color(1.0f));
		}
		commandList->endMarker();

		// Forward lighting, sky, grid -----------------------------------------------------------------------------------
		commandList->beginMarker("Forward");
		commandList->clearTextureFloat(m_HDR, nvrhi::AllSubresources, nvrhi::Color(0.0f));
		DrawMeshes(commandList, m_ForwardPipeline, m_HDRFramebuffer, m_ForwardSet, true, false, 0, viewportSize);
		DrawFullscreen(commandList, m_SkyPipeline, m_HDRFramebuffer, m_SkySet);
		if (m_Settings.ShowGrid)
			DrawFullscreen(commandList, m_GridPipeline, m_HDRFramebuffer, m_FrameOnlySet);
		commandList->endMarker();

		// Tonemapping -------------------------------------------------------------------------------------------------
		commandList->beginMarker("Tonemap");
		DrawFullscreen(commandList, m_TonemapPipeline, m_LDRFramebuffer, m_TonemapSet);
		commandList->endMarker();

		commandList->endMarker();
	}

	void SceneRenderer::DrawMeshes(nvrhi::ICommandList* commandList, nvrhi::IGraphicsPipeline* pipeline, nvrhi::IFramebuffer* framebuffer,
		nvrhi::IBindingSet* passSet, bool bindMaterials, bool shadowCastersOnly, uint32_t cascade, uint32_t viewportSize[2])
	{
		const nvrhi::Viewport viewport(static_cast<float>(viewportSize[0]), static_cast<float>(viewportSize[1]));

		for (const DrawItem& item : m_DrawItems)
		{
			if (shadowCastersOnly && !item.CastShadows)
				continue;

			const Mesh& mesh = *item.MeshAsset;
			for (const Submesh& submesh : mesh.Submeshes)
			{
				nvrhi::GraphicsState state;
				state.setPipeline(pipeline)
					.setFramebuffer(framebuffer)
					.setViewport(nvrhi::ViewportState().addViewportAndScissorRect(viewport))
					.addBindingSet(passSet);
				if (bindMaterials)
					state.addBindingSet(mesh.Materials[submesh.MaterialIndex]->BindingSet);

				nvrhi::VertexBufferBinding vertexBinding;
				vertexBinding.buffer = mesh.VertexBuffer;
				vertexBinding.slot = 0;
				vertexBinding.offset = 0;
				state.addVertexBuffer(vertexBinding);

				nvrhi::IndexBufferBinding indexBinding;
				indexBinding.buffer = mesh.IndexBuffer;
				indexBinding.format = nvrhi::Format::R32_UINT;
				indexBinding.offset = 0;
				state.setIndexBuffer(indexBinding);

				commandList->setGraphicsState(state);

				const GPUPushConstants pushConstants{ item.DrawIndex, cascade, 0, 0 };
				commandList->setPushConstants(&pushConstants, sizeof(pushConstants));
				commandList->drawIndexed(nvrhi::DrawArguments().setVertexCount(submesh.IndexCount).setStartIndexLocation(submesh.IndexOffset));

				if (pipeline == m_ShadowPipeline)
					m_Stats.ShadowDrawCalls++;
				else
					m_Stats.DrawCalls++;
				if (pipeline == m_ForwardPipeline)
					m_Stats.Triangles += submesh.IndexCount / 3;
			}
		}
	}

	void SceneRenderer::DrawFullscreen(nvrhi::ICommandList* commandList, nvrhi::IGraphicsPipeline* pipeline, nvrhi::IFramebuffer* framebuffer,
		nvrhi::IBindingSet* bindingSet, const void* pushConstants, size_t pushConstantsSize)
	{
		const nvrhi::FramebufferInfoEx& info = framebuffer->getFramebufferInfo();
		nvrhi::GraphicsState state;
		state.setPipeline(pipeline)
			.setFramebuffer(framebuffer)
			.setViewport(nvrhi::ViewportState().addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(info.width), static_cast<float>(info.height))))
			.addBindingSet(bindingSet);
		commandList->setGraphicsState(state);
		if (pushConstants)
			commandList->setPushConstants(pushConstants, pushConstantsSize);
		commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
	}

	uint32_t SceneRenderer::ReadEntityID(uint32_t x, uint32_t y)
	{
		if (x >= m_Width || y >= m_Height)
			return NoEntity;

		nvrhi::TextureDesc desc = m_EntityIDs->getDesc();
		desc.setWidth(1).setHeight(1).setIsRenderTarget(false).setInitialState(nvrhi::ResourceStates::CopyDest).setKeepInitialState(true).setDebugName("EntityIDReadback");
		desc.useClearValue = false;
		nvrhi::StagingTextureHandle staging = m_Device->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);

		nvrhi::CommandListHandle commandList = m_Device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
		commandList->open();
		commandList->copyTexture(staging, nvrhi::TextureSlice().setOrigin(0, 0).setSize(1, 1), m_EntityIDs, nvrhi::TextureSlice().setOrigin(x, y).setSize(1, 1));
		commandList->close();
		m_Device->executeCommandList(commandList);
		m_Device->waitForIdle();

		size_t rowPitch = 0;
		const void* mapped = m_Device->mapStagingTexture(staging, nvrhi::TextureSlice().setSize(1, 1), nvrhi::CpuAccessMode::Read, &rowPitch);
		if (!mapped)
			return NoEntity;
		const uint32_t value = *static_cast<const uint32_t*>(mapped);
		m_Device->unmapStagingTexture(staging);
		return value == 0 ? NoEntity : value - 1;
	}

	bool SceneRenderer::ReadFinalImage(std::vector<uint8_t>& pixels)
	{
		nvrhi::TextureDesc desc = m_LDR->getDesc();
		desc.setIsRenderTarget(false).setInitialState(nvrhi::ResourceStates::CopyDest).setKeepInitialState(true).setDebugName("FinalImageReadback");
		desc.useClearValue = false;
		nvrhi::StagingTextureHandle staging = m_Device->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);

		nvrhi::CommandListHandle commandList = m_Device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
		commandList->open();
		commandList->copyTexture(staging, nvrhi::TextureSlice(), m_LDR, nvrhi::TextureSlice());
		commandList->close();
		m_Device->executeCommandList(commandList);
		m_Device->waitForIdle();

		size_t rowPitch = 0;
		const uint8_t* mapped = static_cast<const uint8_t*>(m_Device->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch));
		if (!mapped)
			return false;

		pixels.resize(static_cast<size_t>(m_Width) * m_Height * 4);
		for (uint32_t row = 0; row < m_Height; row++)
			std::memcpy(pixels.data() + static_cast<size_t>(row) * m_Width * 4, mapped + row * rowPitch, static_cast<size_t>(m_Width) * 4);
		m_Device->unmapStagingTexture(staging);
		return true;
	}

}
