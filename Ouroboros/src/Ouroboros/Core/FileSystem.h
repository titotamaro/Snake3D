#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Ouroboros {

	class FileSystem
	{
	public:
		static std::optional<std::string> ReadTextFile(const std::filesystem::path& path);
		static std::optional<std::vector<uint8_t>> ReadBinaryFile(const std::filesystem::path& path);
		static bool WriteTextFile(const std::filesystem::path& path, std::string_view contents);
		static bool WriteBinaryFile(const std::filesystem::path& path, const void* data, size_t size);

		static std::filesystem::path GetExecutablePath();
		static std::filesystem::path GetExecutableDirectory();

		// Locates the engine "Resources" directory: $OB_RESOURCES_DIR, then <exe dir>/Resources.
		static std::filesystem::path GetEngineResourcesDirectory();
		static void SetEngineResourcesDirectory(const std::filesystem::path& path);

		// Returns `path` relative to `base` using forward slashes, or the original path if it is not inside `base`.
		static std::string MakeRelativeGeneric(const std::filesystem::path& path, const std::filesystem::path& base);
	};

}
