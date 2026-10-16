#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Ouroboros {

	enum class LogLevel : uint8_t
	{
		Trace = 0,
		Info,
		Warn,
		Error,
		Critical
	};

	struct LogMessage
	{
		uint64_t Index = 0;
		LogLevel Level = LogLevel::Info;
		std::string Logger;
		std::string Text;
	};

	// Thread-safe ring buffer of recent log messages. Feeds the editor console and the agent command API.
	class LogBuffer
	{
	public:
		explicit LogBuffer(size_t capacity = 2048);

		void Push(LogLevel level, const std::string& logger, const std::string& text);
		void Clear();

		// Returns messages whose index is >= sinceIndex (0 returns everything still buffered).
		std::vector<LogMessage> GetMessages(uint64_t sinceIndex = 0) const;
		uint64_t GetNextIndex() const;
		size_t GetErrorCount() const;

	private:
		mutable std::mutex m_Mutex;
		std::deque<LogMessage> m_Messages;
		size_t m_Capacity;
		uint64_t m_NextIndex = 1;
		size_t m_ErrorCount = 0;
	};

	class Log
	{
	public:
		static void Init();
		static void Shutdown();

		static std::shared_ptr<spdlog::logger>& GetCoreLogger();
		static std::shared_ptr<spdlog::logger>& GetClientLogger();
		static LogBuffer& GetBuffer();

		static const char* LevelToString(LogLevel level);

	private:
		static std::shared_ptr<spdlog::logger> s_CoreLogger;
		static std::shared_ptr<spdlog::logger> s_ClientLogger;
	};

}

#define OB_CORE_TRACE(...) ::Ouroboros::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define OB_CORE_INFO(...) ::Ouroboros::Log::GetCoreLogger()->info(__VA_ARGS__)
#define OB_CORE_WARN(...) ::Ouroboros::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define OB_CORE_ERROR(...) ::Ouroboros::Log::GetCoreLogger()->error(__VA_ARGS__)
#define OB_CORE_CRITICAL(...) ::Ouroboros::Log::GetCoreLogger()->critical(__VA_ARGS__)

#define OB_TRACE(...) ::Ouroboros::Log::GetClientLogger()->trace(__VA_ARGS__)
#define OB_INFO(...) ::Ouroboros::Log::GetClientLogger()->info(__VA_ARGS__)
#define OB_WARN(...) ::Ouroboros::Log::GetClientLogger()->warn(__VA_ARGS__)
#define OB_ERROR(...) ::Ouroboros::Log::GetClientLogger()->error(__VA_ARGS__)
#define OB_CRITICAL(...) ::Ouroboros::Log::GetClientLogger()->critical(__VA_ARGS__)
