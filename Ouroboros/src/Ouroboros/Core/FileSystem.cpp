#include "obpch.h"
#include "Ouroboros/Core/FileSystem.h"

#if defined(OB_PLATFORM_WINDOWS)
	#include <Windows.h>
#elif defined(OB_PLATFORM_MACOS)
	#include <mach-o/dyld.h>
#elif defined(OB_PLATFORM_LINUX)
	#include <unistd.h>
#endif

namespace Ouroboros {

	static std::filesystem::path s_EngineResourcesOverride;

	std::optional<std::string> FileSystem::ReadTextFile(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::in | std::ios::binary);
		if (!stream)
			return std::nullopt;

		std::stringstream buffer;
		buffer << stream.rdbuf();
		return buffer.str();
	}

	std::optional<std::vector<uint8_t>> FileSystem::ReadBinaryFile(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::in | std::ios::binary | std::ios::ate);
		if (!stream)
			return std::nullopt;

		const std::streamsize size = stream.tellg();
		if (size < 0)
			return std::nullopt;

		std::vector<uint8_t> data(static_cast<size_t>(size));
		stream.seekg(0, std::ios::beg);
		if (size > 0 && !stream.read(reinterpret_cast<char*>(data.data()), size))
			return std::nullopt;
		return data;
	}

	bool FileSystem::WriteTextFile(const std::filesystem::path& path, std::string_view contents)
	{
		return WriteBinaryFile(path, contents.data(), contents.size());
	}

	bool FileSystem::WriteBinaryFile(const std::filesystem::path& path, const void* data, size_t size)
	{
		std::error_code error;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), error);

		std::ofstream stream(path, std::ios::out | std::ios::binary | std::ios::trunc);
		if (!stream)
			return false;
		stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
		return static_cast<bool>(stream);
	}

	std::filesystem::path FileSystem::GetExecutablePath()
	{
#if defined(OB_PLATFORM_WINDOWS)
		std::wstring buffer(MAX_PATH, L'\0');
		while (true)
		{
			const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (length == 0)
				return {};
			if (length < buffer.size())
			{
				buffer.resize(length);
				return std::filesystem::path(buffer);
			}
			buffer.resize(buffer.size() * 2);
		}
#elif defined(OB_PLATFORM_MACOS)
		uint32_t size = 0;
		_NSGetExecutablePath(nullptr, &size);
		std::string buffer(size, '\0');
		if (_NSGetExecutablePath(buffer.data(), &size) != 0)
			return {};
		std::error_code error;
		return std::filesystem::canonical(buffer.c_str(), error);
#elif defined(OB_PLATFORM_LINUX)
		std::error_code error;
		return std::filesystem::read_symlink("/proc/self/exe", error);
#endif
	}

	std::filesystem::path FileSystem::GetExecutableDirectory()
	{
		return GetExecutablePath().parent_path();
	}

	std::filesystem::path FileSystem::GetEngineResourcesDirectory()
	{
		if (!s_EngineResourcesOverride.empty())
			return s_EngineResourcesOverride;

		if (const char* environment = std::getenv("OB_RESOURCES_DIR"); environment && *environment)
			return std::filesystem::path(environment);

		return GetExecutableDirectory() / "Resources";
	}

	void FileSystem::SetEngineResourcesDirectory(const std::filesystem::path& path)
	{
		s_EngineResourcesOverride = path;
	}

	std::string FileSystem::MakeRelativeGeneric(const std::filesystem::path& path, const std::filesystem::path& base)
	{
		std::error_code error;
		const std::filesystem::path absolutePath = std::filesystem::weakly_canonical(path, error);
		const std::filesystem::path absoluteBase = std::filesystem::weakly_canonical(base, error);
		std::filesystem::path relative = absolutePath.lexically_relative(absoluteBase);
		if (relative.empty() || relative.native().starts_with(std::filesystem::path("..").native()))
			return path.generic_string();
		return relative.generic_string();
	}

}
