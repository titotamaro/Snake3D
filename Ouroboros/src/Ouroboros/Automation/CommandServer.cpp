#include "obpch.h"
#include "Ouroboros/Automation/CommandServer.h"

#if defined(OB_PLATFORM_WINDOWS)
	#include <WinSock2.h>
	#include <WS2tcpip.h>
	using SocketHandle = SOCKET;
	constexpr SocketHandle InvalidSocket = INVALID_SOCKET;
	static void CloseSocket(SocketHandle socket) { closesocket(socket); }
	static bool WouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
	static void SetNonBlocking(SocketHandle socket)
	{
		u_long mode = 1;
		ioctlsocket(socket, FIONBIO, &mode);
	}
#else
	#include <arpa/inet.h>
	#include <cerrno>
	#include <fcntl.h>
	#include <netinet/in.h>
	#include <sys/select.h>
	#include <sys/socket.h>
	#include <unistd.h>
	using SocketHandle = int;
	constexpr SocketHandle InvalidSocket = -1;
	static void CloseSocket(SocketHandle socket) { close(socket); }
	static bool WouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
	static void SetNonBlocking(SocketHandle socket) { fcntl(socket, F_SETFL, fcntl(socket, F_GETFL, 0) | O_NONBLOCK); }
#endif

namespace Ouroboros {

	namespace {

#if defined(OB_PLATFORM_WINDOWS)
		struct WinsockInitializer
		{
			bool Initialized = false;
			WinsockInitializer()
			{
				WSADATA data;
				Initialized = WSAStartup(MAKEWORD(2, 2), &data) == 0;
			}
			~WinsockInitializer()
			{
				if (Initialized)
					WSACleanup();
			}
		};

		bool EnsureNetworking()
		{
			static WinsockInitializer s_Winsock;
			return s_Winsock.Initialized;
		}
#else
		bool EnsureNetworking() { return true; }
#endif

		constexpr size_t MaxLineLength = 16 * 1024 * 1024;

	}

	CommandServer::CommandServer(CommandProcessor& processor)
		: m_Processor(processor)
	{
	}

	CommandServer::~CommandServer()
	{
		Stop();
	}

	bool CommandServer::Start(uint16_t port)
	{
		if (m_Running)
			return true;
		if (!EnsureNetworking())
		{
			OB_CORE_ERROR("CommandServer: networking could not be initialized");
			return false;
		}

		SocketHandle listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listenSocket == InvalidSocket)
		{
			OB_CORE_ERROR("CommandServer: could not create a socket");
			return false;
		}

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(port);
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

		if (bind(listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(listenSocket, 4) != 0)
		{
			OB_CORE_ERROR("CommandServer: could not listen on 127.0.0.1:{} (port in use?)", port);
			CloseSocket(listenSocket);
			return false;
		}

		socklen_t length = sizeof(address);
		getsockname(listenSocket, reinterpret_cast<sockaddr*>(&address), &length);
		m_Port = ntohs(address.sin_port);
		SetNonBlocking(listenSocket);

		m_ListenSocket = static_cast<intptr_t>(listenSocket);
		m_Running = true;
		m_Thread = std::thread([this]() { NetworkThread(); });
		OB_CORE_INFO("Agent command server listening on 127.0.0.1:{}", m_Port);
		return true;
	}

	void CommandServer::Stop()
	{
		if (!m_Running)
			return;
		m_Running = false;
		if (m_Thread.joinable())
			m_Thread.join();
		CloseSocket(static_cast<SocketHandle>(m_ListenSocket));
		m_ListenSocket = -1;
	}

	void CommandServer::Poll()
	{
		std::deque<IncomingLine> incoming;
		{
			std::scoped_lock lock(m_Mutex);
			incoming.swap(m_Incoming);
		}

		for (IncomingLine& line : incoming)
		{
			const uint64_t connection = line.Connection;
			m_Processor.SubmitLine(line.Line, [this, connection](const nlohmann::json& response)
			{
				std::scoped_lock lock(m_Mutex);
				m_Outgoing.push_back({ connection, response.dump() + "\n" });
			});
		}
	}

	void CommandServer::NetworkThread()
	{
		struct Connection
		{
			uint64_t Id;
			SocketHandle Socket;
			std::string Input;
			std::string Output;
		};

		std::vector<Connection> connections;
		uint64_t nextId = 1;
		const SocketHandle listenSocket = static_cast<SocketHandle>(m_ListenSocket);

		while (m_Running)
		{
			// Collect responses for open connections.
			{
				std::scoped_lock lock(m_Mutex);
				for (OutgoingLine& line : m_Outgoing)
				{
					for (Connection& connection : connections)
					{
						if (connection.Id == line.Connection)
							connection.Output += line.Line;
					}
				}
				m_Outgoing.clear();
			}

			fd_set readSet, writeSet;
			FD_ZERO(&readSet);
			FD_ZERO(&writeSet);
			FD_SET(listenSocket, &readSet);
			SocketHandle maxSocket = listenSocket;
			for (const Connection& connection : connections)
			{
				FD_SET(connection.Socket, &readSet);
				if (!connection.Output.empty())
					FD_SET(connection.Socket, &writeSet);
				maxSocket = std::max(maxSocket, connection.Socket);
			}

			timeval timeout{ 0, 20000 }; // 20 ms keeps response latency low without busy waiting
			const int ready = select(static_cast<int>(maxSocket + 1), &readSet, &writeSet, nullptr, &timeout);
			if (ready < 0)
				continue;

			if (FD_ISSET(listenSocket, &readSet))
			{
				SocketHandle client = accept(listenSocket, nullptr, nullptr);
				if (client != InvalidSocket)
				{
					SetNonBlocking(client);
					connections.push_back({ nextId++, client, {}, {} });
				}
			}

			for (auto it = connections.begin(); it != connections.end();)
			{
				bool closed = false;

				if (FD_ISSET(it->Socket, &readSet))
				{
					char buffer[8192];
					const int received = static_cast<int>(recv(it->Socket, buffer, sizeof(buffer), 0));
					if (received > 0)
					{
						it->Input.append(buffer, static_cast<size_t>(received));
						size_t newline;
						while ((newline = it->Input.find('\n')) != std::string::npos)
						{
							std::string line = it->Input.substr(0, newline);
							it->Input.erase(0, newline + 1);
							if (!line.empty() && line.back() == '\r')
								line.pop_back();
							if (line.empty())
								continue;
							std::scoped_lock lock(m_Mutex);
							m_Incoming.push_back({ it->Id, std::move(line) });
						}
						if (it->Input.size() > MaxLineLength)
							closed = true;
					}
					else if (received == 0 || !WouldBlock())
					{
						closed = true;
					}
				}

				if (!closed && !it->Output.empty() && FD_ISSET(it->Socket, &writeSet))
				{
					const int sent = static_cast<int>(send(it->Socket, it->Output.data(), static_cast<int>(it->Output.size()), 0));
					if (sent > 0)
						it->Output.erase(0, static_cast<size_t>(sent));
					else if (!WouldBlock())
						closed = true;
				}

				if (closed)
				{
					CloseSocket(it->Socket);
					it = connections.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		for (Connection& connection : connections)
		{
			// Flush what is left (best effort) before closing.
			if (!connection.Output.empty())
				send(connection.Socket, connection.Output.data(), static_cast<int>(connection.Output.size()), 0);
			CloseSocket(connection.Socket);
		}
	}

}
