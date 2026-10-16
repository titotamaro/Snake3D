#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace Ouroboros {

	// Thrown by command handlers to report a failure (becomes {"ok": false, "error": "..."}).
	class CommandError : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	// Result of a command handler: either an immediate value, or a function polled once per frame until it produces one
	// (used for commands that must wait for frames to pass, such as screenshots or "wait").
	struct CommandResult
	{
		nlohmann::json Value;
		std::function<std::optional<nlohmann::json>()> Poll;

		CommandResult(nlohmann::json value = nlohmann::json::object())
			: Value(std::move(value))
		{
		}

		static CommandResult Deferred(std::function<std::optional<nlohmann::json>()> poll)
		{
			CommandResult result;
			result.Poll = std::move(poll);
			return result;
		}
	};

	using CommandHandler = std::function<CommandResult(const nlohmann::json& args)>;

	struct CommandInfo
	{
		std::string Name;
		std::string Description;
		std::string Arguments; // human/agent readable argument summary
		CommandHandler Handler;
	};

	// JSON command dispatcher behind the agent automation API.
	//
	// Request:  {"id": <any>, "cmd": "<name>", "args": {...}}
	// Response: {"id": <same>, "ok": true, "result": ...} or {"id": <same>, "ok": false, "error": "..."}
	//
	// Requests are executed in order. A deferred command blocks the requests queued after it (from any source) until it
	// completes, so a script of commands behaves sequentially.
	class CommandProcessor
	{
	public:
		using ResponseCallback = std::function<void(const nlohmann::json& response)>;

		CommandProcessor();

		void Register(const std::string& name, const std::string& description, const std::string& arguments, CommandHandler handler);
		bool HasCommand(const std::string& name) const { return m_Commands.contains(name); }
		std::vector<const CommandInfo*> GetCommands() const;

		// Queues a request; `callback` receives the response (possibly on a later Update).
		void Submit(const nlohmann::json& request, ResponseCallback callback);
		// Parses a JSON line and queues it. Invalid JSON is answered immediately.
		void SubmitLine(const std::string& line, ResponseCallback callback);

		// Executes queued requests and polls deferred ones. Call once per frame on the main thread.
		void Update();
		bool IsIdle() const { return m_Queue.empty() && !m_Pending; }

		// Runs a request immediately (deferred results are not supported here). Used by tests and --exec.
		nlohmann::json ExecuteImmediate(const nlohmann::json& request);

		// Helpers for handlers
		static const nlohmann::json& RequireArgument(const nlohmann::json& args, const char* name);

	private:
		struct QueuedRequest
		{
			nlohmann::json Request;
			ResponseCallback Callback;
		};

		struct PendingRequest
		{
			nlohmann::json Id;
			std::function<std::optional<nlohmann::json>()> Poll;
			ResponseCallback Callback;
		};

		static nlohmann::json MakeResponse(const nlohmann::json& id, bool ok, const nlohmann::json& payload);
		CommandResult Dispatch(const nlohmann::json& request);

	private:
		std::map<std::string, CommandInfo> m_Commands;
		std::vector<QueuedRequest> m_Queue;
		std::optional<PendingRequest> m_Pending;
	};

}
