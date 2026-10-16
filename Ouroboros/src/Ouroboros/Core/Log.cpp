#include "obpch.h"
#include "Ouroboros/Core/Log.h"

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Ouroboros {

	std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

	namespace {

		LogLevel ToLogLevel(spdlog::level::level_enum level)
		{
			switch (level)
			{
				case spdlog::level::trace:
				case spdlog::level::debug:
					return LogLevel::Trace;
				case spdlog::level::info:
					return LogLevel::Info;
				case spdlog::level::warn:
					return LogLevel::Warn;
				case spdlog::level::err:
					return LogLevel::Error;
				case spdlog::level::critical:
					return LogLevel::Critical;
				default:
					return LogLevel::Info;
			}
		}

		class BufferSink : public spdlog::sinks::base_sink<std::mutex>
		{
		protected:
			void sink_it_(const spdlog::details::log_msg& message) override
			{
				Log::GetBuffer().Push(ToLogLevel(message.level),
					std::string(message.logger_name.begin(), message.logger_name.end()),
					std::string(message.payload.begin(), message.payload.end()));
			}

			void flush_() override {}
		};

	}

	LogBuffer::LogBuffer(size_t capacity)
		: m_Capacity(capacity)
	{
	}

	void LogBuffer::Push(LogLevel level, const std::string& logger, const std::string& text)
	{
		std::scoped_lock lock(m_Mutex);
		m_Messages.push_back({ m_NextIndex++, level, logger, text });
		if (level >= LogLevel::Error)
			m_ErrorCount++;
		while (m_Messages.size() > m_Capacity)
			m_Messages.pop_front();
	}

	void LogBuffer::Clear()
	{
		std::scoped_lock lock(m_Mutex);
		m_Messages.clear();
		m_ErrorCount = 0;
	}

	std::vector<LogMessage> LogBuffer::GetMessages(uint64_t sinceIndex) const
	{
		std::scoped_lock lock(m_Mutex);
		std::vector<LogMessage> result;
		for (const LogMessage& message : m_Messages)
		{
			if (message.Index >= sinceIndex)
				result.push_back(message);
		}
		return result;
	}

	uint64_t LogBuffer::GetNextIndex() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_NextIndex;
	}

	size_t LogBuffer::GetErrorCount() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_ErrorCount;
	}

	void Log::Init()
	{
		if (s_CoreLogger)
			return;

		auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		consoleSink->set_pattern("%^[%T] %n: %v%$");
		auto bufferSink = std::make_shared<BufferSink>();

		std::vector<spdlog::sink_ptr> sinks = { consoleSink, bufferSink };

		s_CoreLogger = std::make_shared<spdlog::logger>("OUROBOROS", sinks.begin(), sinks.end());
		s_CoreLogger->set_level(spdlog::level::trace);
		s_CoreLogger->flush_on(spdlog::level::warn);

		s_ClientLogger = std::make_shared<spdlog::logger>("APP", sinks.begin(), sinks.end());
		s_ClientLogger->set_level(spdlog::level::trace);
		s_ClientLogger->flush_on(spdlog::level::warn);
	}

	void Log::Shutdown()
	{
		if (s_CoreLogger)
			s_CoreLogger->flush();
		if (s_ClientLogger)
			s_ClientLogger->flush();
	}

	std::shared_ptr<spdlog::logger>& Log::GetCoreLogger()
	{
		if (!s_CoreLogger)
			Init();
		return s_CoreLogger;
	}

	std::shared_ptr<spdlog::logger>& Log::GetClientLogger()
	{
		if (!s_ClientLogger)
			Init();
		return s_ClientLogger;
	}

	LogBuffer& Log::GetBuffer()
	{
		static LogBuffer s_Buffer;
		return s_Buffer;
	}

	const char* Log::LevelToString(LogLevel level)
	{
		switch (level)
		{
			case LogLevel::Trace:
				return "trace";
			case LogLevel::Info:
				return "info";
			case LogLevel::Warn:
				return "warn";
			case LogLevel::Error:
				return "error";
			case LogLevel::Critical:
				return "critical";
		}
		return "info";
	}

}
