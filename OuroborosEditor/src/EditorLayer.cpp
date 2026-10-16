#include "EditorLayer.h"

#include <Ouroboros/Asset/Image.h>
#include <Ouroboros/Automation/SceneCommands.h>
#include <Ouroboros/Core/Application.h>
#include <Ouroboros/Core/FileSystem.h>
#include <Ouroboros/Core/Input.h>
#include <Ouroboros/ImGui/GameHUD.h>
#include <Ouroboros/ImGui/ImGuiLayer.h>
#include <Ouroboros/ImGui/ImGuiRenderer.h>
#include <Ouroboros/Math/Math.h>
#include <Ouroboros/Project/ProjectExporter.h>
#include <Ouroboros/Renderer/GraphicsContext.h>
#include <Ouroboros/Renderer/Renderer.h>
#include <Ouroboros/Scene/ComponentSerialization.h>
#include <Ouroboros/Scene/Entity.h>
#include <Ouroboros/Scene/SceneSerializer.h>
#include <Ouroboros/Scripting/ScriptRuntime.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <ImGuizmo.h>

#include <glm/gtc/type_ptr.hpp>

namespace Ouroboros {

	using json = nlohmann::json;

	namespace {

		std::filesystem::path DefaultRuntimeExecutable()
		{
			std::filesystem::path runtime = FileSystem::GetExecutableDirectory() / "OuroborosRuntime";
#if defined(OB_PLATFORM_WINDOWS)
			runtime += ".exe";
#endif
			return runtime;
		}

		glm::vec3 ReadVec3(const json& value)
		{
			return value.get<glm::vec3>();
		}

	}

	EditorLayer::EditorLayer(const EditorOptions& options)
		: Layer("EditorLayer"), m_Options(options)
	{
	}

	EditorLayer::~EditorLayer() = default;

	// ------------------------------------------------------------------------------------------------------------------
	// Lifecycle

	void EditorLayer::OnAttach()
	{
		std::string error;
		m_Project = Project::Load(m_Options.ProjectFile, &error);
		if (!m_Project)
		{
			OB_CORE_CRITICAL("The editor needs a project: {}", error);
			Application::Get().Close(1);
			return;
		}
		Project::SetActive(m_Project);
		m_ContentBrowserPanel.SetRoot(m_Project->GetAssetDirectory());
		m_ExportPath = (m_Project->GetProjectDirectory().parent_path() / "Exports" / m_Project->GetConfig().Name).string();

		if (Renderer* renderer = Application::Get().GetRenderer())
		{
			m_SceneRenderer = CreateScope<SceneRenderer>(*renderer);
			m_SceneRenderer->GetSettings().ShowGrid = true;
		}

		if (m_Options.Headless)
		{
			m_ViewportWidth = m_Options.HeadlessWidth;
			m_ViewportHeight = m_Options.HeadlessHeight;
		}

		const std::string& startScene = m_Project->GetConfig().StartScene;
		if (startScene.empty() || !OpenScene(startScene))
			NewScene();

		m_EditorCamera.LookAt(glm::vec3(14.0f, 16.0f, 18.0f), glm::vec3(0.0f));

		RegisterCommands();
		if (m_Options.EnableServer)
		{
			m_Server = CreateScope<CommandServer>(m_Commands);
			if (!m_Server->Start(m_Options.ServerPort))
				m_Server.reset();
		}
		if (!m_Options.ExecFile.empty())
			RunExecFile();

		if (Window* window = Application::Get().GetWindow())
			window->SetTitle("Ouroboros Editor - " + m_Project->GetConfig().Name);
	}

	void EditorLayer::OnDetach()
	{
		if (IsPlaying())
			Stop();
		m_Server.reset();
		m_SceneRenderer.reset();
		Project::SetActive(nullptr);
	}

	void EditorLayer::OnUpdate(Timestep ts)
	{
		m_FrameTime = ts.GetMilliseconds();

		if (m_Server)
			m_Server->Poll();
		m_Commands.Update();

		if (m_ExecRunning && m_ExecCompleted == m_ExecSubmitted && m_Commands.IsIdle())
		{
			m_ExecRunning = false;
			OB_CORE_INFO("Executed {} commands ({} failed)", m_ExecCompleted, m_ExecFailed);
			if (m_Options.ExitAfterExec)
				Application::Get().Close(m_ExecFailed > 0 ? 1 : 0);
		}

		if (m_SceneRenderer)
			m_SceneRenderer->SetViewportSize(m_ViewportWidth, m_ViewportHeight);
		m_EditorCamera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);

		Ref<Scene> scene = GetActiveScene();
		if (!scene)
			return;
		scene->SetViewportSize(m_ViewportWidth, m_ViewportHeight);

		if (IsPlaying())
		{
			m_RuntimeScene->OnUpdateRuntime(ts);
			if (m_RuntimeScene->ConsumeQuitRequest())
			{
				Stop();
				return;
			}
			const std::string pending = m_RuntimeScene->ConsumePendingSceneLoad();
			if (!pending.empty())
			{
				// Scripts switching scenes in play mode replace the runtime copy only; the edited scene is untouched.
				Ref<Scene> next = CreateRef<Scene>();
				std::string error;
				if (SceneSerializer(next).LoadFromFile(Project::ResolveAssetPath(pending), &error))
				{
					m_RuntimeScene->OnRuntimeStop();
					m_RuntimeScene = next;
					m_RuntimeScene->SetViewportSize(m_ViewportWidth, m_ViewportHeight);
					m_RuntimeScene->OnRuntimeStart();
				}
				else
				{
					OB_CORE_ERROR("Scene.Load failed: {}", error);
				}
			}
		}
		else
		{
			const bool acceptInput = m_ViewportHovered && !m_GizmoInUse;
			m_EditorCamera.OnUpdate(ts, acceptInput);
		}
	}

	RenderCamera EditorLayer::GetEditorRenderCamera() const
	{
		RenderCamera camera;
		camera.View = m_EditorCamera.GetView();
		camera.Projection = m_EditorCamera.GetProjection();
		camera.Position = m_EditorCamera.GetPosition();
		camera.Near = m_EditorCamera.GetNear();
		camera.Far = m_EditorCamera.GetFar();
		return camera;
	}

	void EditorLayer::OnRender(nvrhi::ICommandList* commandList)
	{
		Application& application = Application::Get();
		if (nvrhi::IFramebuffer* backBuffer = application.GetBackBufferFramebuffer())
			commandList->clearTextureFloat(application.GetGraphicsContext()->GetCurrentBackBuffer(), nvrhi::AllSubresources, nvrhi::Color(0.08f, 0.08f, 0.09f, 1.0f));

		Ref<Scene> scene = GetActiveScene();
		if (!scene || !m_SceneRenderer)
			return;

		std::optional<RenderCamera> camera;
		if (IsPlaying())
			camera = m_SceneRenderer->GetSceneCamera(*scene);
		if (!camera)
			camera = GetEditorRenderCamera();

		Entity selected = GetSelectedEntity();
		m_SceneRenderer->SetSelectedEntity(selected && !IsPlaying() ? static_cast<uint32_t>(selected.GetHandle()) : 0xFFFFFFFFu);
		m_SceneRenderer->GetSettings().ShowGrid = !IsPlaying();
		m_SceneRenderer->Render(*scene, *camera, commandList);

		if (!m_ScreenshotRequests.empty())
		{
			std::vector<ScreenshotRequest> requests = std::move(m_ScreenshotRequests);
			m_ScreenshotRequests.clear();
			application.SubmitPostFrameTask([this, requests]()
			{
				std::vector<uint8_t> pixels;
				const bool read = m_SceneRenderer->ReadFinalImage(pixels);
				for (const ScreenshotRequest& request : requests)
				{
					const uint32_t width = m_SceneRenderer->GetViewportWidth();
					const uint32_t height = m_SceneRenderer->GetViewportHeight();
					if (read && ImageUtils::WritePNG(request.Path, width, height, pixels.data(), width * 4))
						*request.Result = json{ { "path", request.Path.string() }, { "width", width }, { "height", height } };
					else
						*request.Result = json{ { "error", "could not write '" + request.Path.string() + "'" } };
				}
			});
		}

		if (m_PickRequested)
		{
			m_PickRequested = false;
			const uint32_t handle = m_SceneRenderer->ReadEntityID(m_PickX, m_PickY);
			m_SelectedEntity = 0;
			if (handle != 0xFFFFFFFFu && scene->GetRegistry().valid(static_cast<entt::entity>(handle)))
				m_SelectedEntity = Entity(static_cast<entt::entity>(handle), scene.get()).GetUUID();
		}
	}

	// ------------------------------------------------------------------------------------------------------------------
	// Scene management

	void EditorLayer::NewScene(const std::string& name)
	{
		if (IsPlaying())
			Stop();
		m_EditorScene = CreateRef<Scene>(name);
		m_ScenePath.clear();
		m_SelectedEntity = 0;
		OB_CORE_INFO("New scene '{}'", name);
	}

	bool EditorLayer::OpenScene(const std::string& assetPath, std::string* error)
	{
		if (IsPlaying())
			Stop();

		Ref<Scene> scene = CreateRef<Scene>();
		std::string loadError;
		if (!SceneSerializer(scene).LoadFromFile(Project::ResolveAssetPath(assetPath), &loadError))
		{
			OB_CORE_ERROR("Could not open scene '{}': {}", assetPath, loadError);
			if (error)
				*error = loadError;
			return false;
		}

		m_EditorScene = scene;
		m_ScenePath = assetPath;
		m_SelectedEntity = 0;
		OB_CORE_INFO("Opened scene '{}'", assetPath);
		return true;
	}

	bool EditorLayer::SaveScene(const std::string& assetPath)
	{
		const std::string path = assetPath.empty() ? m_ScenePath : assetPath;
		if (path.empty())
		{
			m_OpenSaveAsModal = true;
			return false;
		}
		if (!SceneSerializer(m_EditorScene).SaveToFile(Project::ResolveAssetPath(path)))
			return false;
		m_ScenePath = path;
		return true;
	}

	void EditorLayer::Play()
	{
		if (IsPlaying() || !m_EditorScene)
			return;
		Input::Reset();
		m_RuntimeScene = Scene::Copy(m_EditorScene);
		m_RuntimeScene->SetViewportSize(m_ViewportWidth, m_ViewportHeight);
		m_RuntimeScene->OnRuntimeStart();
	}

	void EditorLayer::Stop()
	{
		if (!IsPlaying())
			return;
		m_RuntimeScene->OnRuntimeStop();
		m_RuntimeScene.reset();
		Input::Reset();
		if (m_EditorScene && !m_EditorScene->HasEntity(m_SelectedEntity))
			m_SelectedEntity = 0;
	}

	void EditorLayer::SetPaused(bool paused)
	{
		if (IsPlaying())
			m_RuntimeScene->SetPaused(paused);
	}

	Entity EditorLayer::GetSelectedEntity() const
	{
		Ref<Scene> scene = GetActiveScene();
		if (!scene || m_SelectedEntity == 0)
			return {};
		return scene->GetEntityByUUID(m_SelectedEntity);
	}

	void EditorLayer::DuplicateSelected()
	{
		if (Entity selected = GetSelectedEntity())
			m_SelectedEntity = GetActiveScene()->DuplicateEntity(selected).GetUUID();
	}

	void EditorLayer::DeleteSelected()
	{
		if (Entity selected = GetSelectedEntity())
		{
			GetActiveScene()->DestroyEntity(selected);
			m_SelectedEntity = 0;
		}
	}

	void EditorLayer::FocusSelected()
	{
		if (Entity selected = GetSelectedEntity())
		{
			const TransformComponent& transform = selected.GetTransform();
			m_EditorCamera.Focus(transform.Translation, std::max(4.0f, glm::length(transform.Scale) * 3.0f));
		}
	}

	void EditorLayer::CreateEntityFromAsset(const std::filesystem::path& assetPath)
	{
		const std::string path = assetPath.generic_string();
		const std::string extension = assetPath.extension().string();
		Ref<Scene> scene = GetActiveScene();
		if (extension == ".oscene")
		{
			OpenScene(path);
			return;
		}
		if (!scene)
			return;

		if (extension == ".gltf" || extension == ".glb")
		{
			Entity entity = scene->CreateEntity(assetPath.stem().string());
			entity.AddComponent<MeshRendererComponent>().Mesh = path;
			entity.GetTransform().Translation = m_EditorCamera.GetFocalPoint();
			m_SelectedEntity = entity.GetUUID();
		}
		else if (extension == ".hdr")
		{
			Entity entity = scene->CreateEntity("Sky");
			entity.AddComponent<SkyLightComponent>().Environment = path;
			m_SelectedEntity = entity.GetUUID();
		}
		else if (extension == ".wav" || extension == ".mp3" || extension == ".ogg" || extension == ".flac")
		{
			Entity entity = scene->CreateEntity(assetPath.stem().string());
			entity.AddComponent<AudioSourceComponent>().Clip = path;
			m_SelectedEntity = entity.GetUUID();
		}
		else if (extension == ".lua")
		{
			Entity target = GetSelectedEntity();
			if (!target)
				target = scene->CreateEntity(assetPath.stem().string());
			target.AddOrReplaceComponent<ScriptComponent>().Script = path;
			m_SelectedEntity = target.GetUUID();
		}
	}

	// ------------------------------------------------------------------------------------------------------------------
	// Automation

	json EditorLayer::DescribeState() const
	{
		json state = {
			{ "mode", IsPlaying() ? (m_RuntimeScene->IsPaused() ? "paused" : "play") : "edit" },
			{ "scene", m_ScenePath },
			{ "sceneName", m_EditorScene ? m_EditorScene->GetName() : std::string() },
			{ "selection", m_SelectedEntity ? json(static_cast<uint64_t>(m_SelectedEntity)) : json(nullptr) },
			{ "project", m_Project ? m_Project->GetProjectFilePath().string() : std::string() },
			{ "viewport", { m_ViewportWidth, m_ViewportHeight } },
			{ "headless", m_Options.Headless },
			{ "frame", Application::Get().GetFrameCount() }
		};
		if (m_Server)
			state["port"] = m_Server->GetPort();
		return state;
	}

	void EditorLayer::RunExecFile()
	{
		std::ifstream file(m_Options.ExecFile);
		if (!file)
		{
			OB_CORE_ERROR("Could not open command file '{}'", m_Options.ExecFile.string());
			m_ExecFailed++;
			m_ExecRunning = true;
			return;
		}

		std::string line;
		while (std::getline(file, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			const size_t first = line.find_first_not_of(" \t");
			if (first == std::string::npos || line[first] == '#')
				continue;

			m_ExecSubmitted++;
			m_Commands.SubmitLine(line, [this](const json& response)
			{
				m_ExecCompleted++;
				if (!response.value("ok", false))
					m_ExecFailed++;
				std::printf("%s\n", response.dump().c_str());
				std::fflush(stdout);
			});
		}
		m_ExecRunning = true;
	}

	void EditorLayer::RegisterCommands()
	{
		SceneCommands::Register(m_Commands, [this]() { return GetActiveScene(); });

		// Editor state ------------------------------------------------------------------------------------------------
		m_Commands.Register("editor.state", "Editor mode, open scene, selection, viewport and port.", "{}", [this](const json&) -> CommandResult
		{
			return DescribeState();
		});

		m_Commands.Register("editor.play", "Enters play mode (runs a copy of the edited scene).", "{}", [this](const json&) -> CommandResult
		{
			Play();
			return DescribeState();
		});

		m_Commands.Register("editor.stop", "Leaves play mode, discarding runtime changes.", "{}", [this](const json&) -> CommandResult
		{
			Stop();
			return DescribeState();
		});

		m_Commands.Register("editor.pause", "Pauses or resumes play mode.", "{\"paused\"?: bool}", [this](const json& args) -> CommandResult
		{
			if (!IsPlaying())
				throw CommandError("not in play mode");
			SetPaused(args.value("paused", !m_RuntimeScene->IsPaused()));
			return DescribeState();
		});

		m_Commands.Register("editor.step", "While paused, advances play mode by `frames` fixed steps. Completes after the step.",
			"{\"frames\"?: int}", [this](const json& args) -> CommandResult
		{
			if (!IsPlaying() || !m_RuntimeScene->IsPaused())
				throw CommandError("editor.step requires paused play mode");
			m_RuntimeScene->Step(std::max(1, args.value("frames", 1)));
			const uint64_t target = Application::Get().GetFrameCount() + 1;
			return CommandResult::Deferred([this, target]() -> std::optional<json>
			{
				if (Application::Get().GetFrameCount() < target)
					return std::nullopt;
				return DescribeState();
			});
		});

		m_Commands.Register("editor.wait", "Completes after `frames` frames or `seconds` seconds.", "{\"frames\"?: int, \"seconds\"?: number}",
			[](const json& args) -> CommandResult
		{
			const uint64_t frames = args.value("frames", uint64_t(0));
			const double seconds = args.value("seconds", 0.0);
			const uint64_t targetFrame = Application::Get().GetFrameCount() + frames;
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
			return CommandResult::Deferred([targetFrame, deadline]() -> std::optional<json>
			{
				if (Application::Get().GetFrameCount() < targetFrame || std::chrono::steady_clock::now() < deadline)
					return std::nullopt;
				return json{ { "frame", Application::Get().GetFrameCount() } };
			});
		});

		m_Commands.Register("editor.select", "Selects an entity (null clears the selection).", "{\"entity\": id | name | null}", [this](const json& args) -> CommandResult
		{
			if (!args.contains("entity") || args["entity"].is_null())
				m_SelectedEntity = 0;
			else
				m_SelectedEntity = SceneCommands::ResolveEntity(*GetActiveScene(), args).GetUUID();
			return DescribeState();
		});

		m_Commands.Register("editor.camera.get", "Editor camera position, focal point and orientation.", "{}", [this](const json&) -> CommandResult
		{
			return json{
				{ "position", m_EditorCamera.GetPosition() },
				{ "target", m_EditorCamera.GetFocalPoint() },
				{ "distance", m_EditorCamera.GetDistance() },
				{ "pitch", m_EditorCamera.GetPitch() },
				{ "yaw", m_EditorCamera.GetYaw() }
			};
		});

		m_Commands.Register("editor.camera.set", "Moves the editor camera.", "{\"position\": [x,y,z], \"target\": [x,y,z]}", [this](const json& args) -> CommandResult
		{
			m_EditorCamera.LookAt(ReadVec3(CommandProcessor::RequireArgument(args, "position")), ReadVec3(CommandProcessor::RequireArgument(args, "target")));
			return json{ { "position", m_EditorCamera.GetPosition() }, { "target", m_EditorCamera.GetFocalPoint() } };
		});

		m_Commands.Register("editor.camera.focus", "Points the editor camera at an entity.", "{\"entity\": id | name}", [this](const json& args) -> CommandResult
		{
			m_SelectedEntity = SceneCommands::ResolveEntity(*GetActiveScene(), args).GetUUID();
			FocusSelected();
			return json{ { "position", m_EditorCamera.GetPosition() }, { "target", m_EditorCamera.GetFocalPoint() } };
		});

		m_Commands.Register("editor.gizmo", "Sets the transform gizmo mode.", "{\"mode\": \"translate\"|\"rotate\"|\"scale\"|\"none\"}", [this](const json& args) -> CommandResult
		{
			const std::string mode = CommandProcessor::RequireArgument(args, "mode").get<std::string>();
			if (mode == "translate")
				m_GizmoMode = GizmoMode::Translate;
			else if (mode == "rotate")
				m_GizmoMode = GizmoMode::Rotate;
			else if (mode == "scale")
				m_GizmoMode = GizmoMode::Scale;
			else if (mode == "none")
				m_GizmoMode = GizmoMode::None;
			else
				throw CommandError("unknown gizmo mode '" + mode + "'");
			return json{ { "mode", mode } };
		});

		m_Commands.Register("editor.screenshot", "Saves a PNG of the viewport (scene only), or of the whole editor window with \"ui\": true. Completes after the next rendered frame.",
			"{\"path\": string, \"ui\"?: bool}", [this](const json& args) -> CommandResult
		{
			if (!m_SceneRenderer)
				throw CommandError("no GPU available for screenshots");
			std::filesystem::path path = CommandProcessor::RequireArgument(args, "path").get<std::string>();
			if (path.is_relative())
				path = std::filesystem::absolute(path);
			auto result = std::make_shared<std::optional<json>>();
			if (args.value("ui", false))
			{
				const bool queued = Application::Get().CaptureBackBuffer([path, result](const std::vector<uint8_t>& pixels, uint32_t width, uint32_t height)
				{
					if (!pixels.empty() && ImageUtils::WritePNG(path, width, height, pixels.data(), width * 4))
						*result = json{ { "path", path.string() }, { "width", width }, { "height", height } };
					else
						*result = json{ { "error", "could not capture the editor window" } };
				});
				if (!queued)
					throw CommandError("window capture is not available in a headless editor");
			}
			else
			{
				m_ScreenshotRequests.push_back({ path, result });
			}
			return CommandResult::Deferred([result]() -> std::optional<json>
			{
				if (!result->has_value())
					return std::nullopt;
				if (result->value().contains("error"))
					throw CommandError(result->value()["error"].get<std::string>());
				return result->value();
			});
		});

		m_Commands.Register("editor.viewport.resize", "Sets the viewport resolution (headless editors only).", "{\"width\": int, \"height\": int}",
			[this](const json& args) -> CommandResult
		{
			if (!m_Options.Headless)
				throw CommandError("the viewport size follows the viewport panel in the windowed editor");
			m_ViewportWidth = std::clamp(CommandProcessor::RequireArgument(args, "width").get<uint32_t>(), 16u, 8192u);
			m_ViewportHeight = std::clamp(CommandProcessor::RequireArgument(args, "height").get<uint32_t>(), 16u, 8192u);
			return DescribeState();
		});

		m_Commands.Register("editor.quit", "Closes the editor (unsaved changes are lost).", "{\"exitCode\"?: int}", [](const json& args) -> CommandResult
		{
			Application::Get().Close(args.value("exitCode", 0));
			return json{ { "quitting", true } };
		});

		m_Commands.Register("game.ui", "Text currently drawn by scripts with UI.Text (play mode).", "{}", [this](const json&) -> CommandResult
		{
			json elements = json::array();
			if (IsPlaying() && m_RuntimeScene->GetScriptRuntime())
			{
				for (const UITextElement& element : m_RuntimeScene->GetScriptRuntime()->GetUIText())
					elements.push_back({ { "text", element.Text }, { "position", element.Position }, { "size", element.Size }, { "anchor", static_cast<int>(element.Anchor) } });
			}
			return elements;
		});

		// Scenes ------------------------------------------------------------------------------------------------------
		m_Commands.Register("scene.new", "Replaces the edited scene with an empty one.", "{\"name\"?: string}", [this](const json& args) -> CommandResult
		{
			NewScene(args.value("name", std::string("Untitled")));
			return DescribeState();
		});

		m_Commands.Register("scene.open", "Opens a scene (asset-relative path).", "{\"path\": string}", [this](const json& args) -> CommandResult
		{
			std::string error;
			if (!OpenScene(CommandProcessor::RequireArgument(args, "path").get<std::string>(), &error))
				throw CommandError(error);
			return DescribeState();
		});

		m_Commands.Register("scene.save", "Saves the edited scene (to `path` if given, asset-relative).", "{\"path\"?: string}", [this](const json& args) -> CommandResult
		{
			if (IsPlaying())
				throw CommandError("stop play mode before saving");
			const std::string path = args.value("path", m_ScenePath);
			if (path.empty())
				throw CommandError("the scene has no path yet; pass 'path'");
			if (!SaveScene(path))
				throw CommandError("could not save '" + path + "'");
			return DescribeState();
		});

		m_Commands.Register("scene.set", "Replaces the edited scene with .oscene JSON (as returned by scene.get).", "{\"scene\": object}", [this](const json& args) -> CommandResult
		{
			if (IsPlaying())
				throw CommandError("stop play mode before replacing the scene");
			Ref<Scene> scene = CreateRef<Scene>();
			std::string error;
			if (!SceneSerializer(scene).Deserialize(CommandProcessor::RequireArgument(args, "scene"), &error))
				throw CommandError(error);
			m_EditorScene = scene;
			m_SelectedEntity = 0;
			return DescribeState();
		});

		m_Commands.Register("scene.createPreset", "Creates a preset entity (Empty, Cube, Sphere, Plane, Camera, Directional Light, Point Light, Sky Light, Audio Source).",
			"{\"preset\": string}", [this](const json& args) -> CommandResult
		{
			const std::string preset = CommandProcessor::RequireArgument(args, "preset").get<std::string>();
			const auto& presets = SceneHierarchyPanel::GetPresets();
			if (std::find(presets.begin(), presets.end(), preset) == presets.end())
				throw CommandError("unknown preset '" + preset + "'");
			const UUID uuid = SceneHierarchyPanel::CreatePreset(*GetActiveScene(), preset);
			return SceneCommands::DescribeEntity(GetActiveScene()->GetEntityByUUID(uuid));
		});

		// Project -----------------------------------------------------------------------------------------------------
		m_Commands.Register("project.info", "Project settings and paths.", "{}", [this](const json&) -> CommandResult
		{
			const ProjectConfig& config = m_Project->GetConfig();
			return json{
				{ "name", config.Name },
				{ "startScene", config.StartScene },
				{ "projectFile", m_Project->GetProjectFilePath().string() },
				{ "assetDirectory", m_Project->GetAssetDirectory().string() },
				{ "window", { { "title", config.WindowTitle }, { "width", config.WindowWidth }, { "height", config.WindowHeight }, { "fullscreen", config.Fullscreen }, { "vsync", config.VSync } } }
			};
		});

		m_Commands.Register("project.set", "Changes project settings (call project.save to persist).",
			"{\"name\"?, \"startScene\"?, \"windowTitle\"?, \"windowWidth\"?, \"windowHeight\"?, \"fullscreen\"?, \"vsync\"?}", [this](const json& args) -> CommandResult
		{
			ProjectConfig& config = m_Project->GetConfig();
			config.Name = args.value("name", config.Name);
			config.StartScene = args.value("startScene", config.StartScene);
			config.WindowTitle = args.value("windowTitle", config.WindowTitle);
			config.WindowWidth = args.value("windowWidth", config.WindowWidth);
			config.WindowHeight = args.value("windowHeight", config.WindowHeight);
			config.Fullscreen = args.value("fullscreen", config.Fullscreen);
			config.VSync = args.value("vsync", config.VSync);
			return m_Commands.ExecuteImmediate({ { "cmd", "project.info" } })["result"];
		});

		m_Commands.Register("project.save", "Writes the project file.", "{}", [this](const json&) -> CommandResult
		{
			if (!m_Project->Save(m_Project->GetProjectFilePath()))
				throw CommandError("could not write the project file");
			return json{ { "saved", m_Project->GetProjectFilePath().string() } };
		});

		m_Commands.Register("project.export", "Exports a standalone build of the project (runtime + assets).",
			"{\"output\": string, \"executableName\"?: string}", [this](const json& args) -> CommandResult
		{
			ExportOptions options;
			options.OutputDirectory = CommandProcessor::RequireArgument(args, "output").get<std::string>();
			options.ExecutableName = args.value("executableName", std::string());
			options.RuntimeExecutable = DefaultRuntimeExecutable();
			options.EngineResources = FileSystem::GetEngineResourcesDirectory();
			const ExportResult result = ProjectExporter::Export(*m_Project, options);
			if (!result.Success)
				throw CommandError(result.Error);
			return json{ { "executable", result.ExecutablePath.string() }, { "files", result.FileCount } };
		});

		m_Commands.Register("asset.list", "Lists asset files (asset-relative), optionally filtered by extension.",
			"{\"directory\"?: string, \"extension\"?: string}", [this](const json& args) -> CommandResult
		{
			const std::filesystem::path root = m_Project->GetAssetDirectory();
			const std::filesystem::path directory = root / args.value("directory", std::string());
			const std::string extension = args.value("extension", std::string());
			json files = json::array();
			std::error_code error;
			for (auto it = std::filesystem::recursive_directory_iterator(directory, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
			{
				if (it->is_regular_file() && (extension.empty() || it->path().extension() == extension))
					files.push_back(std::filesystem::relative(it->path(), root).generic_string());
			}
			if (error)
				throw CommandError("could not list '" + directory.string() + "': " + error.message());
			return files;
		});

		m_Commands.Register("asset.reload", "Drops cached meshes, textures and environments so they reload from disk.", "{}", [](const json&) -> CommandResult
		{
			if (Renderer* renderer = Application::Get().GetRenderer())
				renderer->ClearAssetCache();
			return json{ { "reloaded", true } };
		});

		// Renderer ----------------------------------------------------------------------------------------------------
		m_Commands.Register("renderer.settings.get", "Current renderer settings.", "{}", [this](const json&) -> CommandResult
		{
			if (!m_SceneRenderer)
				throw CommandError("no GPU available");
			const SceneRendererSettings& settings = m_SceneRenderer->GetSettings();
			return json{
				{ "shadows", settings.EnableShadows }, { "ssao", settings.EnableSSAO }, { "ssaoRadius", settings.SSAORadius },
				{ "ssaoIntensity", settings.SSAOIntensity }, { "ambientColor", settings.AmbientColor }, { "debugView", static_cast<uint32_t>(settings.View) }
			};
		});

		m_Commands.Register("renderer.settings.set", "Changes renderer settings.",
			"{\"shadows\"?, \"ssao\"?, \"ssaoRadius\"?, \"ssaoIntensity\"?, \"ambientColor\"?, \"debugView\"?: 0-5}", [this](const json& args) -> CommandResult
		{
			if (!m_SceneRenderer)
				throw CommandError("no GPU available");
			SceneRendererSettings& settings = m_SceneRenderer->GetSettings();
			settings.EnableShadows = args.value("shadows", settings.EnableShadows);
			settings.EnableSSAO = args.value("ssao", settings.EnableSSAO);
			settings.SSAORadius = args.value("ssaoRadius", settings.SSAORadius);
			settings.SSAOIntensity = args.value("ssaoIntensity", settings.SSAOIntensity);
			if (args.contains("ambientColor"))
				settings.AmbientColor = ReadVec3(args["ambientColor"]);
			if (args.contains("debugView"))
			{
				const uint32_t view = args["debugView"].get<uint32_t>();
				if (view > static_cast<uint32_t>(DebugView::MetallicRoughness))
					throw CommandError("debugView must be between 0 and 5");
				settings.View = static_cast<DebugView>(view);
			}
			return m_Commands.ExecuteImmediate({ { "cmd", "renderer.settings.get" } })["result"];
		});

		m_Commands.Register("renderer.stats", "Statistics of the last rendered frame.", "{}", [this](const json&) -> CommandResult
		{
			if (!m_SceneRenderer)
				throw CommandError("no GPU available");
			const SceneRendererStats& stats = m_SceneRenderer->GetStats();
			GraphicsContext* context = Application::Get().GetGraphicsContext();
			return json{
				{ "drawCalls", stats.DrawCalls }, { "shadowDrawCalls", stats.ShadowDrawCalls }, { "triangles", stats.Triangles },
				{ "meshes", stats.Meshes }, { "frameTimeMs", m_FrameTime },
				{ "gpu", context ? context->GetDeviceName() : std::string() },
				{ "validationErrors", context ? context->GetValidationErrorCount() : 0 }
			};
		});
	}

	// ------------------------------------------------------------------------------------------------------------------
	// Events

	void EditorLayer::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& keyEvent) { return OnKeyPressed(keyEvent); });
		dispatcher.Dispatch<WindowDropEvent>([this](WindowDropEvent& dropEvent)
		{
			for (const std::string& path : dropEvent.GetPaths())
			{
				const std::filesystem::path relative = std::filesystem::path(Project::MakeAssetRelative(path));
				if (relative.is_relative())
					CreateEntityFromAsset(relative);
				else
					OB_CORE_WARN("Dropped file '{}' is not inside the project's asset directory", path);
			}
			return true;
		});
	}

	bool EditorLayer::OnKeyPressed(KeyPressedEvent& event)
	{
		if (event.IsRepeat())
			return false;

		const bool control = Input::IsKeyDown(Key::LeftControl) || Input::IsKeyDown(Key::RightControl);
		const bool shift = Input::IsKeyDown(Key::LeftShift) || Input::IsKeyDown(Key::RightShift);

		if (control)
		{
			switch (event.GetKeyCode())
			{
				case Key::S:
					if (shift)
						m_OpenSaveAsModal = true;
					else if (!IsPlaying())
						SaveScene();
					return true;
				case Key::N:
					NewScene();
					return true;
				case Key::D:
					DuplicateSelected();
					return true;
				case Key::P:
					IsPlaying() ? Stop() : Play();
					return true;
				default:
					return false;
			}
		}

		// Single-key shortcuts only apply to the viewport in edit mode (play mode keys belong to the game).
		if (IsPlaying() || ImGui::GetIO().WantTextInput)
			return false;

		switch (event.GetKeyCode())
		{
			case Key::Q:
				m_GizmoMode = GizmoMode::None;
				return true;
			case Key::W:
				if (!Input::IsMouseButtonDown(Mouse::ButtonRight))
					m_GizmoMode = GizmoMode::Translate;
				return true;
			case Key::E:
				m_GizmoMode = GizmoMode::Rotate;
				return true;
			case Key::R:
				m_GizmoMode = GizmoMode::Scale;
				return true;
			case Key::F:
				FocusSelected();
				return true;
			case Key::Delete:
				DeleteSelected();
				return true;
			default:
				return false;
		}
	}

	// ------------------------------------------------------------------------------------------------------------------
	// UI

	void EditorLayer::OnImGuiRender()
	{
		ImGuiViewport* mainViewport = ImGui::GetMainViewport();
		const ImGuiID dockspace = ImGui::DockSpaceOverViewport(0, mainViewport, ImGuiDockNodeFlags_None);
		if (!m_LayoutInitialized)
		{
			m_LayoutInitialized = true;
			if (ImGui::DockBuilderGetNode(dockspace) == nullptr || ImGui::DockBuilderGetNode(dockspace)->IsLeafNode())
				SetupDefaultLayout(dockspace);
		}

		DrawMenuBar();
		DrawToolbar();

		Ref<Scene> scene = GetActiveScene();
		m_HierarchyPanel.OnImGuiRender(scene, m_SelectedEntity);
		m_InspectorPanel.OnImGuiRender(scene, m_SelectedEntity);
		if (std::optional<std::filesystem::path> opened = m_ContentBrowserPanel.OnImGuiRender())
			CreateEntityFromAsset(*opened);
		m_ConsolePanel.OnImGuiRender();
		if (m_ShowRendererPanel)
			DrawRendererPanel();
		if (m_ShowProjectPanel)
			DrawProjectPanel();
		if (m_ShowImGuiDemo)
			ImGui::ShowDemoWindow(&m_ShowImGuiDemo);

		DrawViewport();
		DrawModals();
	}

	void EditorLayer::SetupDefaultLayout(unsigned int dockspaceId)
	{
		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

		ImGuiID center = dockspaceId;
		ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
		ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);
		ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);
		ImGuiID top = ImGui::DockBuilderSplitNode(center, ImGuiDir_Up, 0.06f, nullptr, &center);
		ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.35f, nullptr, &right);

		ImGui::DockBuilderDockWindow("Toolbar", top);
		ImGui::DockBuilderDockWindow("Viewport", center);
		ImGui::DockBuilderDockWindow("Hierarchy", left);
		ImGui::DockBuilderDockWindow("Inspector", right);
		ImGui::DockBuilderDockWindow("Renderer", rightBottom);
		ImGui::DockBuilderDockWindow("Project", rightBottom);
		ImGui::DockBuilderDockWindow("Content Browser", bottom);
		ImGui::DockBuilderDockWindow("Console", bottom);
		ImGui::DockBuilderFinish(dockspaceId);

		if (ImGuiDockNode* toolbar = ImGui::DockBuilderGetNode(top))
			toolbar->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
	}

	void EditorLayer::DrawMenuBar()
	{
		if (!ImGui::BeginMainMenuBar())
			return;

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Scene", "Ctrl+N"))
				NewScene();
			if (ImGui::BeginMenu("Open Scene"))
			{
				std::error_code error;
				const std::filesystem::path root = m_Project->GetAssetDirectory();
				for (auto it = std::filesystem::recursive_directory_iterator(root, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
				{
					if (it->path().extension() != ".oscene")
						continue;
					const std::string relative = std::filesystem::relative(it->path(), root).generic_string();
					if (ImGui::MenuItem(relative.c_str()))
						OpenScene(relative);
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, !IsPlaying()))
				SaveScene();
			if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S", false, !IsPlaying()))
				m_OpenSaveAsModal = true;
			ImGui::Separator();
			if (ImGui::MenuItem("Export Game..."))
				m_OpenExportModal = true;
			ImGui::Separator();
			if (ImGui::MenuItem("Exit"))
				Application::Get().Close();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, static_cast<bool>(GetSelectedEntity())))
				DuplicateSelected();
			if (ImGui::MenuItem("Delete", "Del", false, static_cast<bool>(GetSelectedEntity())))
				DeleteSelected();
			if (ImGui::BeginMenu("Create"))
			{
				for (const std::string& preset : SceneHierarchyPanel::GetPresets())
				{
					if (ImGui::MenuItem(preset.c_str()))
						m_SelectedEntity = SceneHierarchyPanel::CreatePreset(*GetActiveScene(), preset);
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Reload Assets"))
				Application::Get().GetRenderer()->ClearAssetCache();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View"))
		{
			ImGui::MenuItem("Renderer", nullptr, &m_ShowRendererPanel);
			ImGui::MenuItem("Project", nullptr, &m_ShowProjectPanel);
			ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowImGuiDemo);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			ImGui::TextDisabled("Agent API: %s", m_Server ? ("127.0.0.1:" + std::to_string(m_Server->GetPort())).c_str() : "disabled");
			ImGui::TextDisabled("Send {\"cmd\":\"help\"} for the command list.");
			ImGui::EndMenu();
		}

		const std::string title = (m_ScenePath.empty() ? std::string("<unsaved scene>") : m_ScenePath) + (IsPlaying() ? "  [PLAYING]" : "");
		ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(title.c_str()).x - 20.0f);
		ImGui::TextDisabled("%s", title.c_str());
		ImGui::EndMainMenuBar();
	}

	void EditorLayer::DrawToolbar()
	{
		ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoTitleBar);

		const char* gizmoLabels[] = { "Move (W)", "Rotate (E)", "Scale (R)" };
		for (int i = 0; i < 3; i++)
		{
			const bool active = static_cast<int>(m_GizmoMode) == i;
			if (active)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			if (ImGui::Button(gizmoLabels[i]))
				m_GizmoMode = static_cast<GizmoMode>(i);
			if (active)
				ImGui::PopStyleColor();
			ImGui::SameLine();
		}

		const float buttonsWidth = 220.0f;
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 10.0f, (ImGui::GetWindowWidth() - buttonsWidth) * 0.5f));
		if (!IsPlaying())
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.25f, 1.0f));
			if (ImGui::Button("  Play  "))
				Play();
			ImGui::PopStyleColor();
		}
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
			if (ImGui::Button("  Stop  "))
				Stop();
			ImGui::PopStyleColor();
			if (IsPlaying())
			{
				ImGui::SameLine();
				const bool paused = m_RuntimeScene->IsPaused();
				if (ImGui::Button(paused ? "Resume" : "Pause"))
					SetPaused(!paused);
				ImGui::SameLine();
				ImGui::BeginDisabled(!paused);
				if (ImGui::Button("Step"))
					m_RuntimeScene->Step(1);
				ImGui::EndDisabled();
			}
		}

		ImGui::SameLine();
		ImGui::TextDisabled("  %.1f ms", m_FrameTime);
		ImGui::End();
	}

	void EditorLayer::DrawViewport()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("Viewport");

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 size = ImGui::GetContentRegionAvail();
		m_ViewportBounds[0] = origin.x;
		m_ViewportBounds[1] = origin.y;
		m_ViewportBounds[2] = origin.x + size.x;
		m_ViewportBounds[3] = origin.y + size.y;

		m_ViewportFocused = ImGui::IsWindowFocused();
		m_ViewportHovered = ImGui::IsWindowHovered();

		if (size.x >= 1.0f && size.y >= 1.0f)
		{
			m_ViewportWidth = static_cast<uint32_t>(size.x);
			m_ViewportHeight = static_cast<uint32_t>(size.y);
		}

		if (m_SceneRenderer)
		{
			const ImTextureID texture = static_cast<ImTextureID>(ToImTextureID(m_SceneRenderer->GetFinalImage()));
			ImGui::Image(texture, size);

			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OB_ASSET_PATH"))
					CreateEntityFromAsset(std::filesystem::path(static_cast<const char*>(payload->Data)));
				ImGui::EndDragDropTarget();
			}

			// HUD drawn by the game's scripts
			if (IsPlaying() && m_RuntimeScene->GetScriptRuntime())
			{
				ImGuiLayer* imgui = Application::Get().GetImGuiLayer();
				GameHUD::Draw(ImGui::GetWindowDrawList(), imgui->GetHUDFont(), m_RuntimeScene->GetScriptRuntime()->GetUIText(),
					m_ViewportBounds[0], m_ViewportBounds[1], m_ViewportBounds[2] - m_ViewportBounds[0], m_ViewportBounds[3] - m_ViewportBounds[1]);
			}
		}
		else
		{
			ImGui::TextDisabled("No GPU available");
		}

		if (!IsPlaying())
			DrawGizmo();

		// Click to select (ignored while using the gizmo).
		if (m_ViewportHovered && !IsPlaying() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGui::IsKeyDown(ImGuiKey_LeftAlt))
		{
			const ImVec2 mouse = ImGui::GetMousePos();
			const float x = mouse.x - m_ViewportBounds[0];
			const float y = mouse.y - m_ViewportBounds[1];
			if (x >= 0.0f && y >= 0.0f && x < static_cast<float>(m_ViewportWidth) && y < static_cast<float>(m_ViewportHeight))
			{
				m_PickRequested = true;
				m_PickX = static_cast<uint32_t>(x);
				m_PickY = static_cast<uint32_t>(y);
			}
		}

		ImGui::End();
		ImGui::PopStyleVar();
	}

	void EditorLayer::DrawGizmo()
	{
		m_GizmoInUse = false;
		Entity selected = GetSelectedEntity();
		if (!selected || m_GizmoMode == GizmoMode::None)
			return;

		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect(m_ViewportBounds[0], m_ViewportBounds[1], m_ViewportBounds[2] - m_ViewportBounds[0], m_ViewportBounds[3] - m_ViewportBounds[1]);

		const glm::mat4 view = m_EditorCamera.GetView();
		const glm::mat4 projection = m_EditorCamera.GetProjection();
		TransformComponent& transform = selected.GetTransform();
		glm::mat4 matrix = transform.GetTransform();

		const bool snap = Input::IsKeyDown(Key::LeftControl);
		const float snapValue = m_GizmoMode == GizmoMode::Rotate ? 15.0f : 0.5f;
		const float snapValues[3] = { snapValue, snapValue, snapValue };

		ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
		if (m_GizmoMode == GizmoMode::Rotate)
			operation = ImGuizmo::ROTATE;
		else if (m_GizmoMode == GizmoMode::Scale)
			operation = ImGuizmo::SCALE;

		ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operation, ImGuizmo::LOCAL, glm::value_ptr(matrix), nullptr, snap ? snapValues : nullptr);

		if (ImGuizmo::IsUsing())
		{
			m_GizmoInUse = true;
			glm::vec3 translation, rotation, scale;
			if (Math::DecomposeTransform(matrix, translation, rotation, scale))
			{
				transform.Translation = translation;
				transform.Rotation = rotation;
				transform.Scale = scale;
			}
		}
	}

	void EditorLayer::DrawRendererPanel()
	{
		ImGui::Begin("Renderer", &m_ShowRendererPanel);
		if (!m_SceneRenderer)
		{
			ImGui::TextDisabled("No GPU available");
			ImGui::End();
			return;
		}

		GraphicsContext* context = Application::Get().GetGraphicsContext();
		const SceneRendererStats& stats = m_SceneRenderer->GetStats();
		ImGui::Text("GPU: %s", context->GetDeviceName().c_str());
		ImGui::Text("Frame: %.2f ms (%.0f FPS)", m_FrameTime, m_FrameTime > 0.0f ? 1000.0f / m_FrameTime : 0.0f);
		ImGui::Text("Draw calls: %u (+%u shadow)", stats.DrawCalls, stats.ShadowDrawCalls);
		ImGui::Text("Triangles: %u   Meshes: %u", stats.Triangles, stats.Meshes);
		ImGui::Separator();

		SceneRendererSettings& settings = m_SceneRenderer->GetSettings();
		ImGui::Checkbox("Shadows", &settings.EnableShadows);
		ImGui::Checkbox("SSAO", &settings.EnableSSAO);
		ImGui::SliderFloat("SSAO Radius", &settings.SSAORadius, 0.05f, 2.0f);
		ImGui::SliderFloat("SSAO Intensity", &settings.SSAOIntensity, 0.1f, 4.0f);
		ImGui::ColorEdit3("Ambient (no sky)", glm::value_ptr(settings.AmbientColor));
		const char* views[] = { "Lit", "Albedo", "Normals", "Ambient Occlusion", "Shadow Cascades", "Metallic / Roughness" };
		int view = static_cast<int>(settings.View);
		if (ImGui::Combo("Debug View", &view, views, 6))
			settings.View = static_cast<DebugView>(view);
		bool vsync = context->IsVSync();
		if (ImGui::Checkbox("VSync", &vsync))
			context->SetVSync(vsync);
		ImGui::End();
	}

	void EditorLayer::DrawProjectPanel()
	{
		ImGui::Begin("Project", &m_ShowProjectPanel);
		ProjectConfig& config = m_Project->GetConfig();
		ImGui::InputText("Name", &config.Name);
		ImGui::InputText("Start Scene", &config.StartScene);
		ImGui::SameLine();
		if (ImGui::SmallButton("Use current") && !m_ScenePath.empty())
			config.StartScene = m_ScenePath;
		ImGui::InputText("Window Title", &config.WindowTitle);
		int size[2] = { static_cast<int>(config.WindowWidth), static_cast<int>(config.WindowHeight) };
		if (ImGui::InputInt2("Window Size", size))
		{
			config.WindowWidth = static_cast<uint32_t>(std::max(size[0], 320));
			config.WindowHeight = static_cast<uint32_t>(std::max(size[1], 240));
		}
		ImGui::Checkbox("Fullscreen", &config.Fullscreen);
		ImGui::SameLine();
		ImGui::Checkbox("VSync", &config.VSync);
		if (ImGui::Button("Save Project"))
			m_Project->Save(m_Project->GetProjectFilePath());
		ImGui::SameLine();
		if (ImGui::Button("Export Game..."))
			m_OpenExportModal = true;
		ImGui::Separator();
		ImGui::TextDisabled("Agent API: %s", m_Server ? ("127.0.0.1:" + std::to_string(m_Server->GetPort())).c_str() : "disabled");
		ImGui::End();
	}

	void EditorLayer::DrawModals()
	{
		if (m_OpenSaveAsModal)
		{
			ImGui::OpenPopup("Save Scene As");
			m_SaveAsPath = m_ScenePath.empty() ? std::string("Scenes/NewScene.oscene") : m_ScenePath;
			m_OpenSaveAsModal = false;
		}
		if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Asset-relative path:");
			ImGui::SetNextItemWidth(400.0f);
			ImGui::InputText("##path", &m_SaveAsPath);
			if (ImGui::Button("Save") && !m_SaveAsPath.empty())
			{
				if (!m_SaveAsPath.ends_with(".oscene"))
					m_SaveAsPath += ".oscene";
				SaveScene(m_SaveAsPath);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		if (m_OpenExportModal)
		{
			ImGui::OpenPopup("Export Game");
			m_LastExportMessage.clear();
			m_OpenExportModal = false;
		}
		if (ImGui::BeginPopupModal("Export Game", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Output directory:");
			ImGui::SetNextItemWidth(480.0f);
			ImGui::InputText("##output", &m_ExportPath);
			if (ImGui::Button("Export"))
			{
				ExportOptions options;
				options.OutputDirectory = m_ExportPath;
				options.RuntimeExecutable = DefaultRuntimeExecutable();
				options.EngineResources = FileSystem::GetEngineResourcesDirectory();
				const ExportResult result = ProjectExporter::Export(*m_Project, options);
				m_LastExportMessage = result.Success ? "Exported to " + result.ExecutablePath.string() : "Export failed: " + result.Error;
			}
			ImGui::SameLine();
			if (ImGui::Button("Close"))
				ImGui::CloseCurrentPopup();
			if (!m_LastExportMessage.empty())
				ImGui::TextWrapped("%s", m_LastExportMessage.c_str());
			ImGui::EndPopup();
		}
	}

}
