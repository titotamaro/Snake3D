#include "TestUtilities.h"

#include <Ouroboros/Renderer/GraphicsContext.h>
#include <Ouroboros/Renderer/Renderer.h>
#include <Ouroboros/Renderer/SceneRenderer.h>
#include <Ouroboros/Scene/Entity.h>
#include <Ouroboros/Scene/SceneSerializer.h>

#include <set>

using namespace Ouroboros;

namespace {

	// One headless Vulkan device shared by the GPU tests (created on first use).
	struct GpuEnvironment
	{
		Scope<GraphicsContext> Context;
		Scope<Renderer> SharedRenderer;

		static Scope<GpuEnvironment>& Instance()
		{
			static Scope<GpuEnvironment> s_Environment;
			return s_Environment;
		}

		static GpuEnvironment& Get()
		{
			Scope<GpuEnvironment>& environment = Instance();
			if (!environment)
			{
				environment = CreateScope<GpuEnvironment>();
				GraphicsContextSpecification specification;
				specification.EnableValidation = true;
				environment->Context = GraphicsContext::Create(specification);
				if (environment->Context)
					environment->SharedRenderer = CreateScope<Renderer>(*environment->Context);
			}
			return *environment;
		}

		~GpuEnvironment()
		{
			SharedRenderer.reset();
			Context.reset();
		}
	};

	void RenderFrame(SceneRenderer& sceneRenderer, Scene& scene, const RenderCamera& camera)
	{
		GpuEnvironment& gpu = GpuEnvironment::Get();
		nvrhi::IDevice* device = gpu.Context->GetDevice();
		nvrhi::CommandListHandle commandList = device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
		REQUIRE(gpu.Context->BeginFrame());
		commandList->open();
		sceneRenderer.Render(scene, camera, commandList);
		commandList->close();
		device->executeCommandList(commandList);
		gpu.Context->Present();
		gpu.Context->WaitIdle();
	}

}

namespace Ouroboros::Testing {

	void ShutdownGpuEnvironment()
	{
		GpuEnvironment::Instance().reset();
	}

}

TEST_SUITE("gpu")
{
	TEST_CASE("A headless Vulkan 1.3 device can be created")
	{
		GpuEnvironment& gpu = GpuEnvironment::Get();
		REQUIRE_MESSAGE(gpu.Context, "no Vulkan 1.3 device available (run `ctest -LE gpu` on machines without a GPU)");
		CHECK(gpu.Context->IsHeadless());
		CHECK_FALSE(gpu.Context->GetDeviceName().empty());
		CHECK(gpu.Context->GetCurrentFramebuffer() == nullptr);
		CHECK(gpu.Context->BeginFrame());
		gpu.Context->Present();
	}

	TEST_CASE("Shaders, meshes and environments load and are cached")
	{
		GpuEnvironment& gpu = GpuEnvironment::Get();
		REQUIRE(gpu.SharedRenderer);
		Renderer& renderer = *gpu.SharedRenderer;
		Testing::ScopedProject project;

		for (const char* shader : { "Mesh.vs", "Shadow.vs", "PrePass.ps", "PBR.ps", "Fullscreen.vs", "SSAO.ps", "SSAOBlur.ps", "Skybox.ps",
			"Grid.ps", "Tonemap.ps", "Blit.ps", "ImGui.vs", "ImGui.ps", "EquirectToCube.cs", "CubeDownsample.cs", "Irradiance.cs", "Prefilter.cs", "BrdfLut.cs" })
		{
			CAPTURE(shader);
			CHECK(renderer.GetShader(shader) != nullptr);
		}

		Ref<Mesh> cube = renderer.GetMesh("Primitive:Cube");
		REQUIRE(cube);
		CHECK(cube->IndexCount == 36);
		CHECK(renderer.GetMesh("Primitive:Cube") == cube);

		Ref<Mesh> wall = renderer.GetMesh("Meshes/Wall.gltf");
		REQUIRE(wall);
		CHECK(wall->Materials.size() == 1);
		CHECK(wall->Materials[0]->BindingSet != nullptr);
		CHECK(renderer.GetCachedTextureCount() >= 3);

		Testing::LogErrorCounter errors;
		CHECK(renderer.GetMesh("Meshes/Missing.gltf") == nullptr);
		CHECK(renderer.GetMesh("Meshes/Missing.gltf") == nullptr); // failure is cached and logged once
		CHECK(errors.GetCount() == 1);
		CHECK(renderer.GetMesh("") == nullptr);

		Ref<Environment> environment = renderer.GetEnvironment("Environments/kloofendal_48d_partly_cloudy_puresky_1k.hdr");
		REQUIRE(environment);
		CHECK(environment->PrefilteredMipCount == 6);
		CHECK(environment->Radiance->getDesc().dimension == nvrhi::TextureDimension::TextureCube);
		CHECK(renderer.GetBrdfLut() != nullptr);

		renderer.ClearAssetCache();
		CHECK(renderer.GetCachedMeshCount() == 0);
		CHECK(gpu.Context->GetValidationErrorCount() == 0);
	}

	TEST_CASE("The scene renderer draws the Snake scene and supports picking and readback")
	{
		GpuEnvironment& gpu = GpuEnvironment::Get();
		REQUIRE(gpu.SharedRenderer);
		Testing::ScopedProject project;

		auto scene = CreateRef<Scene>();
		std::string error;
		REQUIRE(SceneSerializer(scene).LoadFromFile(Project::ResolveAssetPath("Scenes/Snake.oscene"), &error));

		SceneRenderer sceneRenderer(*gpu.SharedRenderer);
		sceneRenderer.SetViewportSize(320, 180);
		CHECK(sceneRenderer.GetViewportWidth() == 320);

		std::optional<RenderCamera> camera = sceneRenderer.GetSceneCamera(*scene);
		REQUIRE(camera);
		RenderFrame(sceneRenderer, *scene, *camera);

		const SceneRendererStats& stats = sceneRenderer.GetStats();
		CHECK(stats.Meshes == 3); // board, head, apple (walls are spawned by the game script)
		CHECK(stats.DrawCalls > 0);
		CHECK(stats.ShadowDrawCalls > 0);
		CHECK(stats.Triangles > 1000);

		std::vector<uint8_t> pixels;
		REQUIRE(sceneRenderer.ReadFinalImage(pixels));
		REQUIRE(pixels.size() == 320 * 180 * 4);

		// The middle of the frame shows the green board.
		const size_t center = (90 * 320 + 160) * 4;
		CHECK(pixels[center + 1] > pixels[center + 0]);
		CHECK(pixels[center + 1] > pixels[center + 2]);

		// The image is not uniform (sky, board, models).
		std::set<uint32_t> colors;
		for (size_t i = 0; i < pixels.size(); i += 4 * 37)
			colors.insert(pixels[i] | (pixels[i + 1] << 8) | (pixels[i + 2] << 16));
		CHECK(colors.size() > 50);

		// Picking returns the board in the center and nothing in the sky.
		Entity board = scene->FindEntityByName("Board");
		CHECK(sceneRenderer.ReadEntityID(160, 90) == static_cast<uint32_t>(board.GetHandle()));
		CHECK(sceneRenderer.ReadEntityID(2, 2) == 0xFFFFFFFFu);
		CHECK(sceneRenderer.ReadEntityID(5000, 5000) == 0xFFFFFFFFu);

		CHECK(gpu.Context->GetValidationErrorCount() == 0);
	}

	TEST_CASE("Every debug view and setting combination renders without validation errors")
	{
		GpuEnvironment& gpu = GpuEnvironment::Get();
		REQUIRE(gpu.SharedRenderer);
		Testing::ScopedProject project;

		auto scene = CreateRef<Scene>();
		std::string error;
		REQUIRE(SceneSerializer(scene).LoadFromFile(Project::ResolveAssetPath("Scenes/FeatureTest.oscene"), &error));

		SceneRenderer sceneRenderer(*gpu.SharedRenderer);
		sceneRenderer.SetViewportSize(256, 144);
		const RenderCamera camera = *sceneRenderer.GetSceneCamera(*scene);

		for (uint32_t view = 0; view <= static_cast<uint32_t>(DebugView::MetallicRoughness); view++)
		{
			sceneRenderer.GetSettings().View = static_cast<DebugView>(view);
			RenderFrame(sceneRenderer, *scene, camera);
		}
		sceneRenderer.GetSettings().View = DebugView::None;

		SceneRendererSettings& settings = sceneRenderer.GetSettings();
		settings.EnableShadows = false;
		settings.EnableSSAO = false;
		settings.ShowGrid = true;
		sceneRenderer.SetSelectedEntity(static_cast<uint32_t>(scene->FindEntityByName("Sphere 1").GetHandle()));
		RenderFrame(sceneRenderer, *scene, camera);
		CHECK(sceneRenderer.GetStats().ShadowDrawCalls == 0);

		// Scenes without lights, sky or camera still render (fallback environment and ambient light).
		Scene empty("Empty");
		empty.CreateEntity("Cube").AddComponent<MeshRendererComponent>();
		sceneRenderer.SetViewportSize(64, 64);
		RenderFrame(sceneRenderer, empty, camera);
		CHECK_FALSE(sceneRenderer.GetSceneCamera(empty));

		CHECK(gpu.Context->GetValidationErrorCount() == 0);
	}
}
