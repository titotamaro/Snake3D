#pragma once

#include "Ouroboros/Core/Base.h"
#include "Ouroboros/Renderer/RenderResources.h"
#include "Ouroboros/Renderer/ShaderTypes.h"

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include <optional>
#include <vector>

namespace Ouroboros {

	class Renderer;
	class Scene;

	struct RenderCamera
	{
		glm::mat4 View{ 1.0f };
		glm::mat4 Projection{ 1.0f };
		glm::vec3 Position{ 0.0f };
		float Near = 0.1f;
		float Far = 500.0f;
		float Exposure = 1.0f;
	};

	enum class DebugView : uint32_t
	{
		None = 0,
		Albedo = 1,
		Normals = 2,
		AmbientOcclusion = 3,
		ShadowCascades = 4,
		MetallicRoughness = 5
	};

	struct SceneRendererSettings
	{
		bool EnableShadows = true;
		bool EnableSSAO = true;
		bool ShowGrid = false;
		float SSAORadius = 0.5f;
		float SSAOIntensity = 1.5f;
		float ShadowDepthBias = 0.0005f;
		float ShadowNormalBias = 0.03f;
		glm::vec3 AmbientColor{ 0.08f, 0.09f, 0.11f };
		DebugView View = DebugView::None;
	};

	struct SceneRendererStats
	{
		uint32_t DrawCalls = 0;
		uint32_t Triangles = 0;
		uint32_t Meshes = 0;
		uint32_t ShadowDrawCalls = 0;
	};

	// Renders a Scene into an internal LDR image (RGBA8, sRGB-encoded):
	// shadow cascades -> depth/normal/ID pre-pass -> SSAO -> forward PBR -> sky -> (grid) -> tonemap.
	class SceneRenderer
	{
	public:
		explicit SceneRenderer(Renderer& renderer);
		~SceneRenderer();

		SceneRenderer(const SceneRenderer&) = delete;
		SceneRenderer& operator=(const SceneRenderer&) = delete;

		void SetViewportSize(uint32_t width, uint32_t height);
		uint32_t GetViewportWidth() const { return m_Width; }
		uint32_t GetViewportHeight() const { return m_Height; }

		// Records all passes into `commandList` (which must be open).
		void Render(Scene& scene, const RenderCamera& camera, nvrhi::ICommandList* commandList);

		// Camera of the scene's primary CameraComponent for the current viewport aspect ratio.
		std::optional<RenderCamera> GetSceneCamera(Scene& scene) const;

		nvrhi::ITexture* GetFinalImage() const { return m_LDR; }
		nvrhi::ITexture* GetEntityIDImage() const { return m_EntityIDs; }

		// ECS handle of the entity rendered at pixel (x, y), or entt::null. Waits for the GPU.
		uint32_t ReadEntityID(uint32_t x, uint32_t y);
		// Reads the final image back to the CPU as tightly packed RGBA8. Waits for the GPU.
		bool ReadFinalImage(std::vector<uint8_t>& pixels);

		// The entity outlined as "selected" (ECS handle), or entt::null for none.
		void SetSelectedEntity(uint32_t entityHandle) { m_SelectedEntity = entityHandle; }

		SceneRendererSettings& GetSettings() { return m_Settings; }
		const SceneRendererStats& GetStats() const { return m_Stats; }

	private:
		struct DrawItem
		{
			Ref<Mesh> MeshAsset;
			uint32_t DrawIndex = 0;
			bool CastShadows = true;
		};

		void CreatePipelines();
		void CreateTargets();
		void CreatePassBindingSets(nvrhi::ITexture* environmentTextures[3]);
		void ComputeShadowCascades(const RenderCamera& camera, const glm::vec3& lightDirection, float shadowDistance, GPUFrameConstants& constants) const;
		void DrawMeshes(nvrhi::ICommandList* commandList, nvrhi::IGraphicsPipeline* pipeline, nvrhi::IFramebuffer* framebuffer,
			nvrhi::IBindingSet* passSet, bool bindMaterials, bool shadowCastersOnly, uint32_t cascade, uint32_t viewportSize[2]);
		void DrawFullscreen(nvrhi::ICommandList* commandList, nvrhi::IGraphicsPipeline* pipeline, nvrhi::IFramebuffer* framebuffer,
			nvrhi::IBindingSet* bindingSet, const void* pushConstants = nullptr, size_t pushConstantsSize = 0);

	private:
		Renderer& m_Renderer;
		nvrhi::IDevice* m_Device;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_SelectedEntity = 0xFFFFFFFFu;

		SceneRendererSettings m_Settings;
		SceneRendererStats m_Stats;

		// Layouts
		nvrhi::InputLayoutHandle m_InputLayout;
		nvrhi::InputLayoutHandle m_ShadowInputLayout; // positions only
		nvrhi::BindingLayoutHandle m_DepthPassLayout;   // b0 frame, t0 draws, push constants
		nvrhi::BindingLayoutHandle m_ForwardLayout;     // + shadow map, IBL, BRDF LUT, AO, samplers
		nvrhi::BindingLayoutHandle m_FrameOnlyLayout;   // b0 frame
		nvrhi::BindingLayoutHandle m_SkyLayout;         // b0, t0 cube, s0
		nvrhi::BindingLayoutHandle m_SSAOLayout;        // b0, t0 depth, t1 normals, s0
		nvrhi::BindingLayoutHandle m_BlurLayout;        // b0, t0 ao, t1 depth, s0, push
		nvrhi::BindingLayoutHandle m_TonemapLayout;     // b0, t0 hdr, t1 ids, s0

		// Pipelines
		nvrhi::GraphicsPipelineHandle m_ShadowPipeline;
		nvrhi::GraphicsPipelineHandle m_PrePassPipeline;
		nvrhi::GraphicsPipelineHandle m_ForwardPipeline;
		nvrhi::GraphicsPipelineHandle m_SkyPipeline;
		nvrhi::GraphicsPipelineHandle m_GridPipeline;
		nvrhi::GraphicsPipelineHandle m_SSAOPipeline;
		nvrhi::GraphicsPipelineHandle m_BlurPipeline;
		nvrhi::GraphicsPipelineHandle m_TonemapPipeline;

		// Buffers
		nvrhi::BufferHandle m_FrameConstants;
		nvrhi::BufferHandle m_DrawBuffer;
		uint32_t m_DrawBufferCapacity = 0;

		// Targets
		static constexpr uint32_t ShadowMapSize = 2048;
		nvrhi::TextureHandle m_ShadowMap;
		nvrhi::FramebufferHandle m_ShadowFramebuffers[ShadowCascadeCount];
		nvrhi::TextureHandle m_Depth;
		nvrhi::TextureHandle m_Normals;
		nvrhi::TextureHandle m_EntityIDs;
		nvrhi::TextureHandle m_AORaw;
		nvrhi::TextureHandle m_AOTemp;
		nvrhi::TextureHandle m_AO;
		nvrhi::TextureHandle m_HDR;
		nvrhi::TextureHandle m_LDR;
		nvrhi::FramebufferHandle m_PrePassFramebuffer;
		nvrhi::FramebufferHandle m_AORawFramebuffer;
		nvrhi::FramebufferHandle m_AOTempFramebuffer;
		nvrhi::FramebufferHandle m_AOFramebuffer;
		nvrhi::FramebufferHandle m_HDRFramebuffer;
		nvrhi::FramebufferHandle m_LDRFramebuffer;

		// Binding sets
		nvrhi::BindingSetHandle m_DepthPassSet;
		nvrhi::BindingSetHandle m_ForwardSet;
		nvrhi::BindingSetHandle m_FrameOnlySet;
		nvrhi::BindingSetHandle m_SkySet;
		nvrhi::BindingSetHandle m_SSAOSet;
		nvrhi::BindingSetHandle m_BlurHorizontalSet;
		nvrhi::BindingSetHandle m_BlurVerticalSet;
		nvrhi::BindingSetHandle m_TonemapSet;
		nvrhi::ITexture* m_BoundEnvironment = nullptr;

		std::vector<DrawItem> m_DrawItems;
		std::vector<GPUDrawData> m_DrawData;
	};

}
