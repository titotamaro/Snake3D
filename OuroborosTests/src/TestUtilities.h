#pragma once

#include <Ouroboros/Core/Log.h>
#include <Ouroboros/Project/Project.h>

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <random>
#include <string>

namespace Ouroboros::Testing {

	// Makes the Snake project active for the duration of a test (asset paths resolve into SnakeGame/Assets).
	class ScopedProject
	{
	public:
		ScopedProject()
		{
			m_Project = Project::Load(OB_TEST_PROJECT_FILE);
			REQUIRE(m_Project);
			Project::SetActive(m_Project);
		}

		~ScopedProject() { Project::SetActive(nullptr); }

		const Ref<Project>& Get() const { return m_Project; }

	private:
		Ref<Project> m_Project;
	};

	// A unique temporary directory, removed when the object goes out of scope.
	class TemporaryDirectory
	{
	public:
		TemporaryDirectory()
		{
			std::mt19937_64 random(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
			m_Path = std::filesystem::temp_directory_path() / ("OuroborosTest-" + std::to_string(random()));
			std::filesystem::create_directories(m_Path);
		}

		~TemporaryDirectory()
		{
			std::error_code error;
			std::filesystem::remove_all(m_Path, error);
		}

		const std::filesystem::path& Path() const { return m_Path; }

	private:
		std::filesystem::path m_Path;
	};

	// Counts errors logged while it is alive.
	class LogErrorCounter
	{
	public:
		LogErrorCounter()
			: m_Start(Log::GetBuffer().GetErrorCount())
		{
		}

		size_t GetCount() const { return Log::GetBuffer().GetErrorCount() - m_Start; }

	private:
		size_t m_Start;
	};

	// Releases the shared GPU device used by the "gpu" test suite (called before the test runner exits).
	void ShutdownGpuEnvironment();

	inline std::filesystem::path TestDataPath(const std::string& relative)
	{
		return std::filesystem::path(OB_TEST_DATA_DIR) / relative;
	}

}
