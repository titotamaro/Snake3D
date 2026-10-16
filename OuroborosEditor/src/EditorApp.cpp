#include "EditorLayer.h"

#include <Ouroboros/Core/EntryPoint.h>

#include <cstdio>
#include <cstdlib>

namespace Ouroboros {

	namespace {

		void PrintUsage()
		{
			std::printf(
				"Usage: OuroborosEditor --project <file.oproj> [options]\n"
				"  --headless            run without a window (automation only; renders offscreen)\n"
				"  --size <w>x<h>        headless viewport resolution (default 1280x720)\n"
				"  --port <n>            agent command server port (default %u, 0 = any free port)\n"
				"  --no-server           do not start the agent command server\n"
				"  --exec <file>         run JSON-lines commands from a file at startup (responses go to stdout)\n"
				"  --exit-after-exec     quit when the --exec commands are done (exit code 1 if any failed)\n"
				"  --validation          enable Vulkan/NVRHI validation\n"
				"  --no-audio            run without an audio device\n",
				static_cast<unsigned>(CommandServer::DefaultPort));
		}

	}

	class EditorApplication : public Application
	{
	public:
		EditorApplication(const ApplicationSpecification& specification, const EditorOptions& options)
			: Application(specification)
		{
			if (IsInitialized())
				PushLayer(new EditorLayer(options));
		}
	};

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		ApplicationSpecification specification;
		specification.Name = "Ouroboros Editor";
		specification.Window.Title = "Ouroboros Editor";
		specification.Window.Width = 1600;
		specification.Window.Height = 900;
		specification.Window.Maximized = true;
		specification.EnableDocking = true;
		EditorOptions options;

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
			else if (argument == "--headless")
			{
				options.Headless = true;
				specification.Headless = true;
				specification.FixedTimestep = 1.0f / 60.0f;
			}
			else if (argument == "--size")
			{
				const char* value = next();
				if (!value || std::sscanf(value, "%ux%u", &options.HeadlessWidth, &options.HeadlessHeight) != 2)
					return nullptr;
			}
			else if (argument == "--port")
			{
				const char* value = next();
				if (!value)
					return nullptr;
				options.ServerPort = static_cast<uint16_t>(std::strtoul(value, nullptr, 10));
			}
			else if (argument == "--no-server")
			{
				options.EnableServer = false;
			}
			else if (argument == "--exec")
			{
				const char* value = next();
				if (!value)
					return nullptr;
				options.ExecFile = value;
			}
			else if (argument == "--exit-after-exec")
			{
				options.ExitAfterExec = true;
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
		{
			std::fprintf(stderr, "No project given\n");
			PrintUsage();
			return nullptr;
		}

		// Editor layout is stored per project in a hidden folder (not exported with the game).
		std::error_code error;
		const std::filesystem::path stateDirectory = std::filesystem::absolute(options.ProjectFile, error).parent_path() / ".ouroboros";
		std::filesystem::create_directories(stateDirectory, error);
		specification.ImGuiIniFile = (stateDirectory / "imgui.ini").string();

		specification.CommandLineArgs = args;
		return new EditorApplication(specification, options);
	}

}
