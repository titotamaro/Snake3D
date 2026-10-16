#pragma once

#include "Ouroboros/Automation/CommandProcessor.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Ouroboros {

	// Line-delimited JSON over TCP, bound to 127.0.0.1 only. Each line received is one request for the
	// CommandProcessor; each response is written back as one line on the same connection.
	// Networking runs on a background thread; requests are executed on the main thread by Poll().
	class CommandServer
	{
	public:
		static constexpr uint16_t DefaultPort = 7420;

		explicit CommandServer(CommandProcessor& processor);
		~CommandServer();

		CommandServer(const CommandServer&) = delete;
		CommandServer& operator=(const CommandServer&) = delete;

		// Starts listening. Port 0 picks a free port (see GetPort()). Returns false if the port is unavailable.
		bool Start(uint16_t port = DefaultPort);
		void Stop();
		bool IsRunning() const { return m_Running; }
		uint16_t GetPort() const { return m_Port; }

		// Hands received requests to the processor. Call once per frame on the main thread (before processor.Update()).
		void Poll();

	private:
		void NetworkThread();

		struct IncomingLine
		{
			uint64_t Connection;
			std::string Line;
		};

		struct OutgoingLine
		{
			uint64_t Connection;
			std::string Line;
		};

	private:
		CommandProcessor& m_Processor;
		std::thread m_Thread;
		std::atomic<bool> m_Running{ false };
		uint16_t m_Port = 0;
		intptr_t m_ListenSocket = -1;

		std::mutex m_Mutex;
		std::deque<IncomingLine> m_Incoming;
		std::deque<OutgoingLine> m_Outgoing;
	};

}
