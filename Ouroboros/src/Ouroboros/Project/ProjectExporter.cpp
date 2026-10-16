#include "obpch.h"
#include "Ouroboros/Project/ProjectExporter.h"

namespace Ouroboros {

	namespace {

		bool CopyDirectory(const std::filesystem::path& source, const std::filesystem::path& destination, size_t& fileCount, std::string& error)
		{
			std::error_code errorCode;
			std::filesystem::create_directories(destination, errorCode);
			if (errorCode)
			{
				error = "could not create '" + destination.string() + "': " + errorCode.message();
				return false;
			}

			for (auto it = std::filesystem::recursive_directory_iterator(source, errorCode); !errorCode && it != std::filesystem::recursive_directory_iterator(); it.increment(errorCode))
			{
				const std::filesystem::path relative = it->path().lexically_relative(source);
				// Hidden files and folders (.git, editor state) are not shipped.
				if (!relative.empty() && relative.begin()->string().starts_with("."))
				{
					if (it->is_directory())
						it.disable_recursion_pending();
					continue;
				}

				const std::filesystem::path target = destination / relative;
				if (it->is_directory())
				{
					std::filesystem::create_directories(target, errorCode);
				}
				else if (it->is_regular_file())
				{
					std::filesystem::copy_file(it->path(), target, std::filesystem::copy_options::overwrite_existing, errorCode);
					fileCount++;
				}
				if (errorCode)
				{
					error = "could not copy '" + it->path().string() + "': " + errorCode.message();
					return false;
				}
			}

			if (errorCode)
			{
				error = "could not read '" + source.string() + "': " + errorCode.message();
				return false;
			}
			return true;
		}

	}

	ExportResult ProjectExporter::Export(const Project& project, const ExportOptions& options)
	{
		ExportResult result;
		auto fail = [&result](const std::string& message)
		{
			result.Error = message;
			OB_CORE_ERROR("Export failed: {}", message);
			return result;
		};

		if (options.OutputDirectory.empty())
			return fail("no output directory given");
		if (!std::filesystem::is_regular_file(options.RuntimeExecutable))
			return fail("runtime executable not found at '" + options.RuntimeExecutable.string() + "'");
		if (!std::filesystem::is_directory(options.EngineResources))
			return fail("engine resources not found at '" + options.EngineResources.string() + "'");
		if (!std::filesystem::is_directory(project.GetAssetDirectory()))
			return fail("asset directory not found at '" + project.GetAssetDirectory().string() + "'");
		if (project.GetConfig().StartScene.empty())
			return fail("the project has no start scene");
		if (!std::filesystem::is_regular_file(project.GetAssetDirectory() / project.GetConfig().StartScene))
			return fail("start scene '" + project.GetConfig().StartScene + "' does not exist");

		std::error_code errorCode;
		const std::filesystem::path output = std::filesystem::weakly_canonical(std::filesystem::absolute(options.OutputDirectory, errorCode), errorCode);
		const std::filesystem::path assets = std::filesystem::weakly_canonical(project.GetAssetDirectory(), errorCode);
		const auto [assetEnd, outputEnd] = std::mismatch(assets.begin(), assets.end(), output.begin(), output.end());
		if (assetEnd == assets.end())
			return fail("the output directory must not be inside the asset directory");
		std::filesystem::create_directories(output, errorCode);
		if (errorCode)
			return fail("could not create the output directory: " + errorCode.message());

		std::string error;
		if (!CopyDirectory(project.GetAssetDirectory(), output / "Assets", result.FileCount, error))
			return fail(error);
		if (!CopyDirectory(options.EngineResources, output / "Resources", result.FileCount, error))
			return fail(error);

		// The shipped project always uses the "Assets" folder next to it.
		Project exported = project;
		exported.GetConfig().AssetDirectory = "Assets";
		if (!exported.Save(output / "Game.oproj"))
			return fail("could not write the project file");
		result.FileCount++;

		std::string executableName = options.ExecutableName.empty() ? project.GetConfig().Name : options.ExecutableName;
		std::replace_if(executableName.begin(), executableName.end(), [](char c) { return c == '/' || c == '\\' || c == ':' || c == ' '; }, '_');
		if (executableName.empty())
			executableName = "Game";
		std::filesystem::path executable = output / executableName;
		if (options.RuntimeExecutable.has_extension())
			executable.replace_extension(options.RuntimeExecutable.extension());

		std::filesystem::copy_file(options.RuntimeExecutable, executable, std::filesystem::copy_options::overwrite_existing, errorCode);
		if (errorCode)
			return fail("could not copy the runtime: " + errorCode.message());
		result.FileCount++;

		result.Success = true;
		result.ExecutablePath = executable;
		OB_CORE_INFO("Exported '{}' to '{}' ({} files)", project.GetConfig().Name, output.string(), result.FileCount);
		return result;
	}

}
