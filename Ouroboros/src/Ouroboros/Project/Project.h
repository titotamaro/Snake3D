#pragma once

#include "Ouroboros/Core/Base.h"

#include <filesystem>
#include <string>

namespace Ouroboros {

	struct ProjectConfig
	{
		std::string Name = "Untitled";
		std::string StartScene;                       // asset-relative path to the scene loaded by the runtime
		std::filesystem::path AssetDirectory = "Assets"; // relative to the project file

		std::string WindowTitle = "Ouroboros Game";
		uint32_t WindowWidth = 1280;
		uint32_t WindowHeight = 720;
		bool Fullscreen = false;
		bool VSync = true;
	};

	// A game project (.oproj, JSON). One project is active at a time; asset paths are resolved against it.
	class Project
	{
	public:
		static constexpr const char* FileExtension = ".oproj";

		ProjectConfig& GetConfig() { return m_Config; }
		const ProjectConfig& GetConfig() const { return m_Config; }

		const std::filesystem::path& GetProjectFilePath() const { return m_ProjectFilePath; }
		std::filesystem::path GetProjectDirectory() const { return m_ProjectFilePath.parent_path(); }
		std::filesystem::path GetAssetDirectory() const { return GetProjectDirectory() / m_Config.AssetDirectory; }

		bool Save(const std::filesystem::path& path);

		static Ref<Project> Load(const std::filesystem::path& path, std::string* error = nullptr);
		static Ref<Project> Create(const std::filesystem::path& projectFilePath, const ProjectConfig& config);

		static Ref<Project> GetActive() { return s_ActiveProject; }
		static void SetActive(const Ref<Project>& project) { s_ActiveProject = project; }

		// Resolves an asset-relative path ("Scenes/Main.oscene") to an absolute path inside the active project.
		// Absolute paths are returned unchanged; without an active project paths are relative to the working directory.
		static std::filesystem::path ResolveAssetPath(const std::filesystem::path& assetPath);
		// Converts an absolute path inside the asset directory back to an asset-relative generic path.
		static std::string MakeAssetRelative(const std::filesystem::path& path);

	private:
		ProjectConfig m_Config;
		std::filesystem::path m_ProjectFilePath;

		static Ref<Project> s_ActiveProject;
	};

}
