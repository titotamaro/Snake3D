#pragma once

#include "Ouroboros/Project/Project.h"

#include <filesystem>
#include <string>

namespace Ouroboros {

	struct ExportOptions
	{
		std::filesystem::path OutputDirectory;
		std::filesystem::path RuntimeExecutable;     // the OuroborosRuntime binary to ship
		std::filesystem::path EngineResources;       // engine Resources directory (shaders, fonts)
		std::string ExecutableName;                  // defaults to the project name
	};

	struct ExportResult
	{
		bool Success = false;
		std::string Error;
		std::filesystem::path ExecutablePath;
		size_t FileCount = 0;
	};

	// Produces a standalone, distributable build of a project:
	//   <Output>/<Name>[.exe]   the runtime player (no editing ability)
	//   <Output>/Game.oproj     the project file (the player loads the .oproj next to it)
	//   <Output>/Assets/...     the project's assets
	//   <Output>/Resources/...  engine resources
	class ProjectExporter
	{
	public:
		static ExportResult Export(const Project& project, const ExportOptions& options);
	};

}
