#pragma once

#include "Ouroboros/Core/Base.h"
#include "Ouroboros/Renderer/RenderResources.h"

#include <nvrhi/nvrhi.h>

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Ouroboros {

	class GraphicsContext;
	struct Image;
	struct ImageHDR;

	// Shared GPU state: shader modules, samplers, default textures, common binding layouts and the asset caches for
	// meshes, textures and environments. One Renderer exists per GraphicsContext.
	class Renderer
	{
	public:
		explicit Renderer(GraphicsContext& context);
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		nvrhi::IDevice* GetDevice() const { return m_Device; }
		GraphicsContext& GetContext() const { return m_Context; }

		// Loads Resources/Shaders/<name>.spv (e.g. "PBR.ps"); the stage is taken from the suffix. Cached.
		nvrhi::ShaderHandle GetShader(const std::string& name);

		nvrhi::ISampler* GetLinearClampSampler() const { return m_LinearClampSampler; }
		nvrhi::ISampler* GetLinearWrapSampler() const { return m_LinearWrapSampler; }
		nvrhi::ISampler* GetPointClampSampler() const { return m_PointClampSampler; }
		nvrhi::ISampler* GetShadowSampler() const { return m_ShadowSampler; }

		nvrhi::IBindingLayout* GetMaterialLayout() const { return m_MaterialLayout; }
		nvrhi::ITexture* GetBrdfLut() const { return m_BrdfLut; }
		const Ref<Texture2D>& GetWhiteTexture() const { return m_WhiteTexture; }
		const Ref<Environment>& GetFallbackEnvironment() const { return m_FallbackEnvironment; }

		// Asset caches. Paths are asset-relative (resolved against the active project) or "Primitive:<Name>".
		// Failures are logged once and return nullptr.
		Ref<Mesh> GetMesh(const std::string& path);
		Ref<Environment> GetEnvironment(const std::string& path);
		Ref<Mesh> CreateMesh(const MeshData& data, const std::string& debugName);
		// Drops every cached asset so the next request reloads it from disk.
		void ClearAssetCache();

		// Draws `source` stretched over the whole `framebuffer` (used to present the scene image).
		void Blit(nvrhi::ICommandList* commandList, nvrhi::ITexture* source, nvrhi::IFramebuffer* framebuffer);

		size_t GetCachedMeshCount() const { return m_Meshes.size(); }
		size_t GetCachedTextureCount() const { return m_Textures.size(); }

	private:
		Ref<Texture2D> CreateTexture(nvrhi::ICommandList* commandList, const Image& image, bool srgb, const std::string& debugName);
		Ref<Texture2D> GetTexture(nvrhi::ICommandList* commandList, const TextureSource& source, bool srgb);
		Ref<Material> CreateMaterial(nvrhi::ICommandList* commandList, const MaterialData& data);
		Ref<Environment> CreateEnvironment(const ImageHDR& image, const std::string& debugName);
		nvrhi::TextureHandle CreateCubeTexture(uint32_t size, uint32_t mips, const char* name);
		void CreateBrdfLut();
		void CreateFallbackEnvironment();
		void Submit(nvrhi::ICommandList* commandList);

	private:
		GraphicsContext& m_Context;
		nvrhi::IDevice* m_Device;

		std::unordered_map<std::string, nvrhi::ShaderHandle> m_Shaders;

		nvrhi::SamplerHandle m_LinearClampSampler;
		nvrhi::SamplerHandle m_LinearWrapSampler;
		nvrhi::SamplerHandle m_PointClampSampler;
		nvrhi::SamplerHandle m_ShadowSampler;

		nvrhi::BindingLayoutHandle m_MaterialLayout;
		nvrhi::BindingLayoutHandle m_IBLLayout;
		nvrhi::BindingLayoutHandle m_BrdfLayout;
		nvrhi::ComputePipelineHandle m_EquirectPipeline;
		nvrhi::ComputePipelineHandle m_DownsamplePipeline;
		nvrhi::ComputePipelineHandle m_IrradiancePipeline;
		nvrhi::ComputePipelineHandle m_PrefilterPipeline;

		nvrhi::BindingLayoutHandle m_BlitLayout;
		std::unordered_map<uint32_t, nvrhi::GraphicsPipelineHandle> m_BlitPipelines; // by framebuffer format

		nvrhi::TextureHandle m_BrdfLut;
		Ref<Texture2D> m_WhiteTexture;
		Ref<Texture2D> m_FlatNormalTexture;
		Ref<Environment> m_FallbackEnvironment;

		std::unordered_map<std::string, Ref<Mesh>> m_Meshes;
		std::unordered_map<std::string, Ref<Texture2D>> m_Textures;
		std::unordered_map<std::string, Ref<Environment>> m_Environments;
		std::unordered_set<std::string> m_FailedAssets;
	};

}
