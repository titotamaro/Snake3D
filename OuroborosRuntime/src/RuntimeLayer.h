#pragma once

#include <Ouroboros/Core/Layer.h>
#include <Ouroboros/Project/Project.h>
#include <Ouroboros/Renderer/SceneRenderer.h>
#include <Ouroboros/Scene/Scene.h>

#include <filesystem>
#include <string>

namespace Ouroboros {

	struct RuntimeOptions
	{
		std::filesystem::path ProjectFile;
		std::string SceneOverride;          // asset path; defaults to the project's start scene
		std::filesystem::path ScreenshotPath; // written after the last frame when --frames is used
		uint32_t HeadlessWidth = 1280;
		uint32_t HeadlessHeight = 720;
		bool TestMode = false;              // exit code 1 if scripts or the engine reported errors
		uint32_t DebugView = 0;             // SceneRenderer DebugView (0 = lit)
	};

	// Plays a project: loads the start scene, runs it and presents it full screen. No editing ability.
	class RuntimeLayer : public Layer
	{
	public:
		explicit RuntimeLayer(const RuntimeOptions& options);

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(Timestep ts) override;
		void OnRender(nvrhi::ICommandList* commandList) override;
		void OnImGuiRender() override;

	private:
		bool LoadScene(const std::string& assetPath);
		std::pair<uint32_t, uint32_t> GetViewportSize() const;
		void Finish(int exitCode);
		void TakeScreenshot();
		int ComputeTestExitCode() const;

	private:
		RuntimeOptions m_Options;
		Ref<Project> m_Project;
		Ref<Scene> m_Scene;
		Scope<SceneRenderer> m_SceneRenderer;
		bool m_Failed = false;
		size_t m_InitialErrorCount = 0;
	};

}
