#include "RuntimeLayer.h"

#include <Ouroboros/Asset/Image.h>
#include <Ouroboros/Core/Application.h>
#include <Ouroboros/ImGui/GameHUD.h>
#include <Ouroboros/ImGui/ImGuiLayer.h>
#include <Ouroboros/Renderer/GraphicsContext.h>
#include <Ouroboros/Renderer/Renderer.h>
#include <Ouroboros/Scene/SceneSerializer.h>
#include <Ouroboros/Math/Math.h>
#include <Ouroboros/Scripting/ScriptRuntime.h>

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

namespace Ouroboros {

	RuntimeLayer::RuntimeLayer(const RuntimeOptions& options)
		: Layer("RuntimeLayer"), m_Options(options)
	{
	}

	void RuntimeLayer::OnAttach()
	{
		m_InitialErrorCount = Log::GetBuffer().GetErrorCount();

		std::string error;
		m_Project = Project::Load(m_Options.ProjectFile, &error);
		if (!m_Project)
		{
			Finish(1);
			return;
		}
		Project::SetActive(m_Project);

		Application& application = Application::Get();
		if (Window* window = application.GetWindow())
			window->SetTitle(m_Project->GetConfig().WindowTitle);

		if (Renderer* renderer = application.GetRenderer())
		{
			m_SceneRenderer = CreateScope<SceneRenderer>(*renderer);
			m_SceneRenderer->GetSettings().View = static_cast<DebugView>(m_Options.DebugView);
		}

		const std::string scene = m_Options.SceneOverride.empty() ? m_Project->GetConfig().StartScene : m_Options.SceneOverride;
		if (!LoadScene(scene))
			Finish(1);
	}

	void RuntimeLayer::OnDetach()
	{
		if (m_Scene)
			m_Scene->OnRuntimeStop();
		m_Scene.reset();
		m_SceneRenderer.reset();
		Project::SetActive(nullptr);
	}

	bool RuntimeLayer::LoadScene(const std::string& assetPath)
	{
		if (m_Scene)
			m_Scene->OnRuntimeStop();

		Ref<Scene> scene = CreateRef<Scene>();
		std::string error;
		if (!SceneSerializer(scene).LoadFromFile(Project::ResolveAssetPath(assetPath), &error))
		{
			OB_CORE_ERROR("Could not load scene '{}': {}", assetPath, error);
			return false;
		}

		m_Scene = scene;
		const auto [width, height] = GetViewportSize();
		m_Scene->SetViewportSize(width, height);
		m_Scene->OnRuntimeStart();
		OB_CORE_INFO("Playing scene '{}'", assetPath);
		return true;
	}

	std::pair<uint32_t, uint32_t> RuntimeLayer::GetViewportSize() const
	{
		if (Window* window = Application::Get().GetWindow())
			return { window->GetWidth(), window->GetHeight() };
		return { m_Options.HeadlessWidth, m_Options.HeadlessHeight };
	}

	void RuntimeLayer::Finish(int exitCode)
	{
		m_Failed = m_Failed || exitCode != 0;
		Application::Get().Close(exitCode);
	}

	int RuntimeLayer::ComputeTestExitCode() const
	{
		const ScriptRuntime* scripts = m_Scene ? m_Scene->GetScriptRuntime() : nullptr;
		const size_t engineErrors = Log::GetBuffer().GetErrorCount() - m_InitialErrorCount;
		const uint32_t validationErrors = Application::Get().GetGraphicsContext() ? Application::Get().GetGraphicsContext()->GetValidationErrorCount() : 0;
		if ((scripts && scripts->GetErrorCount() > 0) || engineErrors > 0 || validationErrors > 0)
		{
			OB_CORE_ERROR("Test run failed: {} script errors, {} logged errors, {} validation errors",
				scripts ? scripts->GetErrorCount() : 0, engineErrors, validationErrors);
			return 1;
		}
		return 0;
	}

	void RuntimeLayer::OnUpdate(Timestep ts)
	{
		if (!m_Scene || m_Failed)
			return;

		Application& application = Application::Get();
		const auto [width, height] = GetViewportSize();
		if (width > 0 && height > 0)
		{
			m_Scene->SetViewportSize(width, height);
			if (m_SceneRenderer)
				m_SceneRenderer->SetViewportSize(width, height);
		}

		m_Scene->OnUpdateRuntime(ts);

		if (m_Scene->ConsumeQuitRequest())
		{
			Finish(m_Options.TestMode ? ComputeTestExitCode() : 0);
			return;
		}

		const std::string pendingScene = m_Scene->ConsumePendingSceneLoad();
		if (!pendingScene.empty() && !LoadScene(pendingScene))
		{
			Finish(1);
			return;
		}

		// Last frame of a bounded run: capture a screenshot and compute the test result.
		const uint64_t maxFrames = application.GetSpecification().MaxFrames;
		if (maxFrames > 0 && application.GetFrameCount() + 1 >= maxFrames)
		{
			if (!m_Options.ScreenshotPath.empty())
				application.SubmitPostFrameTask([this]() { TakeScreenshot(); });
			if (m_Options.TestMode)
				application.SetExitCode(ComputeTestExitCode());
		}
	}

	void RuntimeLayer::OnRender(nvrhi::ICommandList* commandList)
	{
		if (!m_Scene || !m_SceneRenderer)
			return;

		std::optional<RenderCamera> camera = m_SceneRenderer->GetSceneCamera(*m_Scene);
		if (!camera)
		{
			// No camera in the scene: look at the origin from a default position.
			camera = RenderCamera();
			camera->View = glm::lookAtRH(glm::vec3(0.0f, 5.0f, 10.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			const float aspect = static_cast<float>(m_SceneRenderer->GetViewportWidth()) / static_cast<float>(m_SceneRenderer->GetViewportHeight());
			camera->Projection = Math::PerspectiveProjection(glm::radians(45.0f), aspect, 0.1f, 500.0f);
			camera->Position = glm::vec3(0.0f, 5.0f, 10.0f);
		}

		m_SceneRenderer->Render(*m_Scene, *camera, commandList);

		if (nvrhi::IFramebuffer* backBuffer = Application::Get().GetBackBufferFramebuffer())
			Application::Get().GetRenderer()->Blit(commandList, m_SceneRenderer->GetFinalImage(), backBuffer);
	}

	void RuntimeLayer::OnImGuiRender()
	{
		if (!m_Scene || !m_Scene->GetScriptRuntime())
			return;

		ImGuiLayer* imgui = Application::Get().GetImGuiLayer();
		const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
		GameHUD::Draw(ImGui::GetForegroundDrawList(), imgui->GetHUDFont(), m_Scene->GetScriptRuntime()->GetUIText(), 0.0f, 0.0f, displaySize.x, displaySize.y);
	}

	void RuntimeLayer::TakeScreenshot()
	{
		if (!m_SceneRenderer)
		{
			OB_CORE_ERROR("Cannot take a screenshot without a GPU");
			return;
		}

		std::vector<uint8_t> pixels;
		if (!m_SceneRenderer->ReadFinalImage(pixels) ||
			!ImageUtils::WritePNG(m_Options.ScreenshotPath, m_SceneRenderer->GetViewportWidth(), m_SceneRenderer->GetViewportHeight(), pixels.data(), m_SceneRenderer->GetViewportWidth() * 4))
		{
			OB_CORE_ERROR("Could not write screenshot '{}'", m_Options.ScreenshotPath.string());
			return;
		}
		OB_CORE_INFO("Screenshot written to '{}'", m_Options.ScreenshotPath.string());
	}

}
