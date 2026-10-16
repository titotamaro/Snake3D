#include "RuntimeLayer.h"

#include <Ouroboros/Core/EntryPoint.h>
#include <Ouroboros/Core/FileSystem.h>

#include <charconv>
#include <cstring>

namespace Ouroboros {

	namespace {

		void PrintUsage()
		{
			std::printf(
				"Usage: OuroborosRuntime [options]\n"
				"  --project <file.oproj>  project to play (default: the .oproj next to the executable)\n"
				"  --scene <asset path>    scene to start instead of the project's start scene\n"
				"  --headless              render offscreen without a window\n"
				"  --frames <n>            quit after n frames\n"
				"  --screenshot <file.png> save the last frame (with --frames)\n"
				"  --size <w>x<h>          offscreen resolution for --headless (default 1280x720)\n"
				"  --test                  exit with code 1 if any script, engine or validation error occurred\n"
				"  --validation            enable Vulkan/NVRHI validation\n"
				"  --no-audio              run without an audio device\n");
		}

		std::filesystem::path FindProjectNextToExecutable()
		{
			std::error_code error;
			for (const auto& entry : std::filesystem::directory_iterator(FileSystem::GetExecutableDirectory(), error))
			{
				if (entry.is_regular_file() && entry.path().extension() == Project::FileExtension)
					return entry.path();
			}
			return {};
		}

	}

	class RuntimeApplication : public Application
	{
	public:
		RuntimeApplication(const ApplicationSpecification& specification, const RuntimeOptions& options)
			: Application(specification)
		{
			if (IsInitialized())
				PushLayer(new RuntimeLayer(options));
		}
	};

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		ApplicationSpecification specification;
		specification.Name = "Ouroboros Runtime";
		RuntimeOptions options;

		for (int i = 1; i < args.Count; i++)
		{
			const std::string argument = args[i];
			auto next = [&]() -> const char*
			{
				if (i + 1 >= args.Count)
				{
					std::fprintf(stderr, "Missing value for %s\n", argument.c_str());
					return nullptr;
				}
				return args[++i];
			};

			if (argument == "--project")
			{
				const char* value = next();
				if (!value)
					return nullptr;
				options.ProjectFile = value;
			}
			else if (argument == "--scene")
			{
				const char* value = next();
				if (!value)
					return nullptr;
				options.SceneOverride = value;
			}
			else if (argument == "--headless")
			{
				// Headless runs are deterministic: every frame advances the game by exactly 1/60 s.
				specification.Headless = true;
				specification.FixedTimestep = 1.0f / 60.0f;
			}
			else if (argument == "--frames")
			{
				const char* value = next();
				if (!value)
					return nullptr;
				specification.MaxFrames = std::strtoull(value, nullptr, 10);
			}
			else if (argument == "--screenshot")
			{
				const char* value = next();
				if (!value)
					return nullptr;
				options.ScreenshotPath = value;
			}
			else if (argument == "--size")
			{
				const char* value = next();
				if (!value || std::sscanf(value, "%ux%u", &options.HeadlessWidth, &options.HeadlessHeight) != 2)
					return nullptr;
			}
			else if (argument == "--debug-view")
			{
				const char* value = next();
				if (!value)
					return nullptr;
				options.DebugView = static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
			}
			else if (argument == "--test")
			{
				options.TestMode = true;
			}
			else if (argument == "--validation")
			{
				specification.EnableValidation = true;
			}
			else if (argument == "--no-audio")
			{
				specification.EnableAudioDevice = false;
			}
			else if (argument == "--help" || argument == "-h")
			{
				PrintUsage();
				return nullptr;
			}
			else
			{
				std::fprintf(stderr, "Unknown argument '%s'\n", argument.c_str());
				PrintUsage();
				return nullptr;
			}
		}

		if (options.ProjectFile.empty())
			options.ProjectFile = FindProjectNextToExecutable();
		if (options.ProjectFile.empty())
		{
			std::fprintf(stderr, "No project given and no .oproj found next to the executable\n");
			PrintUsage();
			return nullptr;
		}

		// Window settings come from the project file.
		if (Ref<Project> project = Project::Load(options.ProjectFile))
		{
			const ProjectConfig& config = project->GetConfig();
			specification.Name = config.Name;
			specification.Window.Title = config.WindowTitle;
			specification.Window.Width = config.WindowWidth;
			specification.Window.Height = config.WindowHeight;
			specification.Window.Fullscreen = config.Fullscreen;
			specification.VSync = config.VSync;
		}

		specification.CommandLineArgs = args;
		specification.EnableImGui = true;
		return new RuntimeApplication(specification, options);
	}

}
