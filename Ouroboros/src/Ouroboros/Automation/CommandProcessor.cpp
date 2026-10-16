#include "obpch.h"
#include "Ouroboros/Automation/CommandProcessor.h"

namespace Ouroboros {

	using json = nlohmann::json;

	CommandProcessor::CommandProcessor()
	{
		Register("ping", "Checks that the command server is alive.", "{}", [](const json&) -> CommandResult
		{
			return json{ { "pong", true }, { "version", OB_VERSION_STRING } };
		});

		Register("help", "Lists every command with its description and arguments.", "{\"command\"?: string}", [this](const json& args) -> CommandResult
		{
			json commands = json::array();
			const std::string filter = args.value("command", std::string());
			for (const auto& [name, info] : m_Commands)
			{
				if (!filter.empty() && name != filter)
					continue;
				commands.push_back({ { "name", name }, { "description", info.Description }, { "args", info.Arguments } });
			}
			if (!filter.empty() && commands.empty())
				throw CommandError("unknown command '" + filter + "'");
			return commands;
		});
	}

	void CommandProcessor::Register(const std::string& name, const std::string& description, const std::string& arguments, CommandHandler handler)
	{
		m_Commands[name] = CommandInfo{ name, description, arguments, std::move(handler) };
	}

	std::vector<const CommandInfo*> CommandProcessor::GetCommands() const
	{
		std::vector<const CommandInfo*> commands;
		for (const auto& [name, info] : m_Commands)
			commands.push_back(&info);
		return commands;
	}

	const json& CommandProcessor::RequireArgument(const json& args, const char* name)
	{
		auto it = args.find(name);
		if (it == args.end() || it->is_null())
			throw CommandError(std::string("missing argument '") + name + "'");
		return *it;
	}

	json CommandProcessor::MakeResponse(const json& id, bool ok, const json& payload)
	{
		json response = { { "id", id }, { "ok", ok } };
		response[ok ? "result" : "error"] = payload;
		return response;
	}

	CommandResult CommandProcessor::Dispatch(const json& request)
	{
		if (!request.is_object())
			throw CommandError("request must be a JSON object");
		auto command = request.find("cmd");
		if (command == request.end() || !command->is_string())
			throw CommandError("request needs a string 'cmd'");

		auto it = m_Commands.find(command->get<std::string>());
		if (it == m_Commands.end())
			throw CommandError("unknown command '" + command->get<std::string>() + "' (use 'help')");

		json args = request.value("args", json::object());
		if (!args.is_object())
			throw CommandError("'args' must be a JSON object");
		return it->second.Handler(args);
	}

	void CommandProcessor::Submit(const json& request, ResponseCallback callback)
	{
		m_Queue.push_back({ request, std::move(callback) });
	}

	void CommandProcessor::SubmitLine(const std::string& line, ResponseCallback callback)
	{
		json request = json::parse(line, nullptr, false);
		if (request.is_discarded())
		{
			callback(MakeResponse(nullptr, false, "invalid JSON"));
			return;
		}
		Submit(request, std::move(callback));
	}

	void CommandProcessor::Update()
	{
		if (m_Pending)
		{
			std::optional<json> value;
			try
			{
				value = m_Pending->Poll();
			}
			catch (const std::exception& exception)
			{
				m_Pending->Callback(MakeResponse(m_Pending->Id, false, exception.what()));
				m_Pending.reset();
				return;
			}
			if (!value)
				return;
			m_Pending->Callback(MakeResponse(m_Pending->Id, true, *value));
			m_Pending.reset();
		}

		while (!m_Queue.empty() && !m_Pending)
		{
			QueuedRequest queued = std::move(m_Queue.front());
			m_Queue.erase(m_Queue.begin());

			const json id = queued.Request.is_object() ? queued.Request.value("id", json()) : json();
			try
			{
				CommandResult result = Dispatch(queued.Request);
				if (result.Poll)
					m_Pending = PendingRequest{ id, std::move(result.Poll), std::move(queued.Callback) };
				else
					queued.Callback(MakeResponse(id, true, result.Value));
			}
			catch (const json::exception& exception)
			{
				queued.Callback(MakeResponse(id, false, std::string("invalid argument: ") + exception.what()));
			}
			catch (const std::exception& exception)
			{
				queued.Callback(MakeResponse(id, false, exception.what()));
			}
		}
	}

	json CommandProcessor::ExecuteImmediate(const json& request)
	{
		const json id = request.is_object() ? request.value("id", json()) : json();
		try
		{
			CommandResult result = Dispatch(request);
			if (result.Poll)
				return MakeResponse(id, false, "this command must be run through the command queue");
			return MakeResponse(id, true, result.Value);
		}
		catch (const json::exception& exception)
		{
			return MakeResponse(id, false, std::string("invalid argument: ") + exception.what());
		}
		catch (const std::exception& exception)
		{
			return MakeResponse(id, false, exception.what());
		}
	}

}
