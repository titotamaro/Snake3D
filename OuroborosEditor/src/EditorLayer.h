#pragma once

#include "Panels/ConsolePanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/SceneHierarchyPanel.h"

#include <Ouroboros/Automation/CommandProcessor.h>
#include <Ouroboros/Automation/CommandServer.h>
#include <Ouroboros/Core/Layer.h>
#include <Ouroboros/Project/Project.h>
#include <Ouroboros/Renderer/EditorCamera.h>
#include <Ouroboros/Renderer/SceneRenderer.h>
#include <Ouroboros/Scene/Scene.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Ouroboros {

	struct EditorOptions
	{
		std::filesystem::path ProjectFile;
		bool Headless = false;
		uint32_t HeadlessWidth = 1280;
		uint32_t HeadlessHeight = 720;
		bool EnableServer = true;
		uint16_t ServerPort = CommandServer::DefaultPort;
		std::filesystem::path ExecFile;   // JSON-lines file of commands run at startup
		bool ExitAfterExec = false;
	};

	enum class GizmoMode : int
	{
		None = -1,
		Translate = 0,
		Rotate = 1,
		Scale = 2
	};

	class EditorLayer : public Layer
	{
	public:
		explicit EditorLayer(const EditorOptions& options);
		~EditorLayer() override;

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(Timestep ts) override;
		void OnRender(nvrhi::ICommandList* commandList) override;
		void OnImGuiRender() override;
		void OnEvent(Event& event) override;

	private:
		// Scene management
		void NewScene(const std::string& name = "Untitled");
		bool OpenScene(const std::string& assetPath, std::string* error = nullptr);
		bool SaveScene(const std::string& assetPath = std::string());
		void Play();
		void Stop();
		void SetPaused(bool paused);
		bool IsPlaying() const { return m_RuntimeScene != nullptr; }
		Ref<Scene> GetActiveScene() const { return m_RuntimeScene ? m_RuntimeScene : m_EditorScene; }

		// Selection & editing
		Entity GetSelectedEntity() const;
		void SetSelectedEntity(UUID uuid) { m_SelectedEntity = uuid; }
		void DuplicateSelected();
		void DeleteSelected();
		void FocusSelected();
		void CreateEntityFromAsset(const std::filesystem::path& assetPath);

		// Automation
		void RegisterCommands();
		void RunExecFile();
		nlohmann::json DescribeState() const;

		// UI
		void SetupDefaultLayout(unsigned int dockspaceId);
		void DrawMenuBar();
		void DrawToolbar();
		void DrawViewport();
		void DrawGizmo();
		void DrawRendererPanel();
		void DrawProjectPanel();
		void DrawModals();
		bool OnKeyPressed(KeyPressedEvent& event);

		RenderCamera GetEditorRenderCamera() const;

	private:
		EditorOptions m_Options;
		Ref<Project> m_Project;
		std::string m_ScenePath; // asset-relative path of the edited scene (empty if never saved)

		Ref<Scene> m_EditorScene;
		Ref<Scene> m_RuntimeScene;
		UUID m_SelectedEntity = 0;

		Scope<SceneRenderer> m_SceneRenderer;
		EditorCamera m_EditorCamera;
		GizmoMode m_GizmoMode = GizmoMode::Translate;
		bool m_GizmoInUse = false;

		// Viewport
		uint32_t m_ViewportWidth = 1280;
		uint32_t m_ViewportHeight = 720;
		float m_ViewportBounds[4] = { 0, 0, 0, 0 }; // min x, min y, max x, max y (screen space)
		bool m_ViewportFocused = false;
		bool m_ViewportHovered = false;
		bool m_PickRequested = false;
		uint32_t m_PickX = 0, m_PickY = 0;

		// Automation
		CommandProcessor m_Commands;
		Scope<CommandServer> m_Server;
		struct ScreenshotRequest
		{
			std::filesystem::path Path;
			std::shared_ptr<std::optional<nlohmann::json>> Result;
		};
		std::vector<ScreenshotRequest> m_ScreenshotRequests;
		size_t m_ExecSubmitted = 0;
		size_t m_ExecCompleted = 0;
		size_t m_ExecFailed = 0;
		bool m_ExecRunning = false;

		// Panels
		SceneHierarchyPanel m_HierarchyPanel;
		InspectorPanel m_InspectorPanel;
		ContentBrowserPanel m_ContentBrowserPanel;
		ConsolePanel m_ConsolePanel;
		bool m_ShowRendererPanel = true;
		bool m_ShowProjectPanel = true;
		bool m_ShowImGuiDemo = false;

		// Modals
		bool m_OpenSaveAsModal = false;
		bool m_OpenExportModal = false;
		std::string m_SaveAsPath;
		std::string m_ExportPath;
		std::string m_LastExportMessage;

		float m_FrameTime = 0.0f;
		bool m_LayoutInitialized = false;
	};

}
