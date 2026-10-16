#include "obpch.h"
#include "Ouroboros/Project/Project.h"

#include "Ouroboros/Core/FileSystem.h"

#include <nlohmann/json.hpp>

namespace Ouroboros {

	using json = nlohmann::json;

	Ref<Project> Project::s_ActiveProject;

	bool Project::Save(const std::filesystem::path& path)
	{
		json data = {
			{ "Ouroboros", "Project" },
			{ "Version", 1 },
			{ "Name", m_Config.Name },
			{ "StartScene", m_Config.StartScene },
			{ "AssetDirectory", m_Config.AssetDirectory.generic_string() },
			{ "Window", {
				{ "Title", m_Config.WindowTitle },
				{ "Width", m_Config.WindowWidth },
				{ "Height", m_Config.WindowHeight },
				{ "Fullscreen", m_Config.Fullscreen },
				{ "VSync", m_Config.VSync }
			} }
		};

		if (!FileSystem::WriteTextFile(path, data.dump(1, '\t') + "\n"))
		{
			OB_CORE_ERROR("Failed to write project file '{}'", path.string());
			return false;
		}

		std::error_code errorCode;
		m_ProjectFilePath = std::filesystem::absolute(path, errorCode);
		return true;
	}

	Ref<Project> Project::Load(const std::filesystem::path& path, std::string* error)
	{
		auto fail = [error](const std::string& message) -> Ref<Project>
		{
			if (error)
				*error = message;
			OB_CORE_ERROR("{}", message);
			return nullptr;
		};

		std::optional<std::string> text = FileSystem::ReadTextFile(path);
		if (!text)
			return fail("Could not read project file '" + path.string() + "'");

		json data = json::parse(*text, nullptr, false);
		if (data.is_discarded() || !data.is_object() || data.value("Ouroboros", std::string()) != "Project")
			return fail("'" + path.string() + "' is not a valid Ouroboros project file");

		Ref<Project> project = CreateRef<Project>();
		ProjectConfig& config = project->m_Config;
		try
		{
			config.Name = data.value("Name", config.Name);
			config.StartScene = data.value("StartScene", config.StartScene);
			config.AssetDirectory = data.value("AssetDirectory", std::string("Assets"));
			if (auto window = data.find("Window"); window != data.end() && window->is_object())
			{
				config.WindowTitle = window->value("Title", config.WindowTitle);
				config.WindowWidth = window->value("Width", config.WindowWidth);
				config.WindowHeight = window->value("Height", config.WindowHeight);
				config.Fullscreen = window->value("Fullscreen", config.Fullscreen);
				config.VSync = window->value("VSync", config.VSync);
			}
		}
		catch (const json::exception& exception)
		{
			return fail("Invalid project file '" + path.string() + "': " + exception.what());
		}

		std::error_code errorCode;
		project->m_ProjectFilePath = std::filesystem::absolute(path, errorCode);
		OB_CORE_INFO("Loaded project '{}' from '{}'", config.Name, project->m_ProjectFilePath.string());
		return project;
	}

	Ref<Project> Project::Create(const std::filesystem::path& projectFilePath, const ProjectConfig& config)
	{
		Ref<Project> project = CreateRef<Project>();
		project->m_Config = config;
		if (!project->Save(projectFilePath))
			return nullptr;

		std::error_code errorCode;
		std::filesystem::create_directories(project->GetAssetDirectory(), errorCode);
		return project;
	}

	std::filesystem::path Project::ResolveAssetPath(const std::filesystem::path& assetPath)
	{
		if (assetPath.empty() || assetPath.is_absolute())
			return assetPath;
		if (s_ActiveProject)
			return s_ActiveProject->GetAssetDirectory() / assetPath;
		return assetPath;
	}

	std::string Project::MakeAssetRelative(const std::filesystem::path& path)
	{
		if (!s_ActiveProject)
			return path.generic_string();
		return FileSystem::MakeRelativeGeneric(path, s_ActiveProject->GetAssetDirectory());
	}

}
