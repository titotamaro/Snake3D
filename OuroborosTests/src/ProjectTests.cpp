#include "TestUtilities.h"

#include <Ouroboros/Core/FileSystem.h>
#include <Ouroboros/Project/Project.h>
#include <Ouroboros/Project/ProjectExporter.h>

using namespace Ouroboros;

TEST_SUITE("Project")
{
	TEST_CASE("Project files round-trip")
	{
		Testing::TemporaryDirectory directory;
		ProjectConfig config;
		config.Name = "Test Game";
		config.StartScene = "Scenes/Main.oscene";
		config.WindowTitle = "Title";
		config.WindowWidth = 640;
		config.WindowHeight = 480;
		config.Fullscreen = true;
		config.VSync = false;

		Ref<Project> created = Project::Create(directory.Path() / "Test.oproj", config);
		REQUIRE(created);
		CHECK(std::filesystem::is_directory(directory.Path() / "Assets"));

		std::string error;
		Ref<Project> loaded = Project::Load(directory.Path() / "Test.oproj", &error);
		REQUIRE_MESSAGE(loaded, error);
		CHECK(loaded->GetConfig().Name == "Test Game");
		CHECK(loaded->GetConfig().StartScene == "Scenes/Main.oscene");
		CHECK(loaded->GetConfig().WindowWidth == 640);
		CHECK(loaded->GetConfig().Fullscreen);
		CHECK_FALSE(loaded->GetConfig().VSync);
		CHECK(loaded->GetAssetDirectory() == directory.Path() / "Assets");
	}

	TEST_CASE("Invalid project files are rejected")
	{
		Testing::TemporaryDirectory directory;
		std::string error;
		CHECK_FALSE(Project::Load(directory.Path() / "missing.oproj", &error));
		FileSystem::WriteTextFile(directory.Path() / "scene.oproj", R"({ "Ouroboros": "Scene" })");
		CHECK_FALSE(Project::Load(directory.Path() / "scene.oproj", &error));
		FileSystem::WriteTextFile(directory.Path() / "types.oproj", R"({ "Ouroboros": "Project", "Window": { "Width": "wide" } })");
		CHECK_FALSE(Project::Load(directory.Path() / "types.oproj", &error));
		CHECK(error.find("Invalid project file") != std::string::npos);
	}

	TEST_CASE("Asset paths resolve against the active project")
	{
		Project::SetActive(nullptr);
		CHECK(Project::ResolveAssetPath("a/b.txt") == std::filesystem::path("a/b.txt"));
		CHECK(Project::MakeAssetRelative("x/y") == "x/y");

		Testing::ScopedProject project;
		const std::filesystem::path resolved = Project::ResolveAssetPath("Scenes/Snake.oscene");
		CHECK(std::filesystem::exists(resolved));
		CHECK(Project::MakeAssetRelative(resolved) == "Scenes/Snake.oscene");
		CHECK(Project::ResolveAssetPath(resolved) == resolved); // absolute paths are unchanged
		CHECK(Project::ResolveAssetPath("").empty());
	}

	TEST_CASE("Exporting produces a standalone build")
	{
		Testing::ScopedProject project;
		Testing::TemporaryDirectory directory;

		// A stand-in runtime binary (the real one is exercised by the ExportGame integration test).
		const std::filesystem::path runtime = directory.Path() / "Runtime.exe";
		FileSystem::WriteTextFile(runtime, "runtime");

		ExportOptions options;
		options.OutputDirectory = directory.Path() / "Export";
		options.RuntimeExecutable = runtime;
		options.EngineResources = FileSystem::GetEngineResourcesDirectory();
		options.ExecutableName = "My Game";

		const ExportResult result = ProjectExporter::Export(*project.Get(), options);
		REQUIRE_MESSAGE(result.Success, result.Error);
		CHECK(result.ExecutablePath == options.OutputDirectory / "My_Game.exe");
		CHECK(std::filesystem::exists(result.ExecutablePath));
		CHECK(std::filesystem::exists(options.OutputDirectory / "Game.oproj"));
		CHECK(std::filesystem::exists(options.OutputDirectory / "Assets" / "Scenes" / "Snake.oscene"));
		CHECK(std::filesystem::exists(options.OutputDirectory / "Assets" / "Meshes" / "Apple.bin"));
		CHECK(std::filesystem::exists(options.OutputDirectory / "Resources" / "Shaders" / "PBR.ps.spv"));
		CHECK(result.FileCount > 30);

		Ref<Project> exported = Project::Load(options.OutputDirectory / "Game.oproj");
		REQUIRE(exported);
		CHECK(exported->GetConfig().StartScene == project.Get()->GetConfig().StartScene);
		CHECK(exported->GetConfig().AssetDirectory == std::filesystem::path("Assets"));
	}

	TEST_CASE("Export validates its inputs")
	{
		Testing::ScopedProject project;
		Testing::TemporaryDirectory directory;
		const std::filesystem::path runtime = directory.Path() / "Runtime.exe";
		FileSystem::WriteTextFile(runtime, "runtime");

		ExportOptions options;
		options.RuntimeExecutable = runtime;
		options.EngineResources = FileSystem::GetEngineResourcesDirectory();
		CHECK_FALSE(ProjectExporter::Export(*project.Get(), options).Success); // no output directory

		options.OutputDirectory = directory.Path() / "Out";
		options.RuntimeExecutable = directory.Path() / "missing.exe";
		CHECK(ProjectExporter::Export(*project.Get(), options).Error.find("runtime executable") != std::string::npos);

		options.RuntimeExecutable = runtime;
		options.EngineResources = directory.Path() / "NoResources";
		CHECK(ProjectExporter::Export(*project.Get(), options).Error.find("engine resources") != std::string::npos);

		options.EngineResources = FileSystem::GetEngineResourcesDirectory();
		options.OutputDirectory = project.Get()->GetAssetDirectory() / "Export";
		CHECK(ProjectExporter::Export(*project.Get(), options).Error.find("inside the asset directory") != std::string::npos);
		CHECK_FALSE(std::filesystem::exists(options.OutputDirectory));

		options.OutputDirectory = directory.Path() / "Out";
		Project withoutScene = *project.Get();
		withoutScene.GetConfig().StartScene.clear();
		CHECK(ProjectExporter::Export(withoutScene, options).Error.find("start scene") != std::string::npos);
	}
}
