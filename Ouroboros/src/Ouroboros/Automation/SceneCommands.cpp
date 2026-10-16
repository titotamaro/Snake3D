#include "obpch.h"
#include "Ouroboros/Automation/SceneCommands.h"

#include "Ouroboros/Core/Input.h"
#include "Ouroboros/Scene/ComponentRegistry.h"
#include "Ouroboros/Scene/Entity.h"
#include "Ouroboros/Scene/SceneSerializer.h"
#include "Ouroboros/Scripting/ScriptRuntime.h"

namespace Ouroboros {

	using json = nlohmann::json;

	namespace {

		const ComponentInfo& RequireComponentType(const json& args)
		{
			const std::string type = CommandProcessor::RequireArgument(args, "type").get<std::string>();
			const ComponentInfo* info = ComponentRegistry::Find(type);
			if (!info)
				throw CommandError("unknown component type '" + type + "' (use component.types)");
			return *info;
		}

		KeyCode RequireKey(const json& args)
		{
			const json& key = CommandProcessor::RequireArgument(args, "key");
			if (key.is_number_integer())
				return static_cast<KeyCode>(key.get<int>());
			const KeyCode code = Input::KeyFromName(key.get<std::string>());
			if (code == 0)
				throw CommandError("unknown key '" + key.get<std::string>() + "'");
			return code;
		}

	}

	Entity SceneCommands::ResolveEntity(Scene& scene, const json& args)
	{
		const json* reference = nullptr;
		for (const char* key : { "entity", "id", "name" })
		{
			if (auto it = args.find(key); it != args.end() && !it->is_null())
			{
				reference = &*it;
				break;
			}
		}
		if (!reference)
			throw CommandError("missing argument 'entity' (an entity id or name)");

		Entity entity;
		if (reference->is_number_unsigned() || reference->is_number_integer())
			entity = scene.GetEntityByUUID(reference->get<uint64_t>());
		else if (reference->is_string())
			entity = scene.FindEntityByName(reference->get<std::string>());
		if (!entity)
			throw CommandError("entity " + reference->dump() + " not found");
		return entity;
	}

	json SceneCommands::DescribeEntity(Entity entity)
	{
		json components = json::array();
		for (const ComponentInfo& info : ComponentRegistry::GetAll())
		{
			if (info.Has(entity))
				components.push_back(info.Name);
		}
		return json{
			{ "id", static_cast<uint64_t>(entity.GetUUID()) },
			{ "name", entity.GetName() },
			{ "components", components }
		};
	}

	void SceneCommands::Register(CommandProcessor& processor, std::function<Ref<Scene>()> getScene)
	{
		auto scene = [getScene]() -> Ref<Scene>
		{
			Ref<Scene> current = getScene();
			if (!current)
				throw CommandError("no scene is open");
			return current;
		};

		// Scene -------------------------------------------------------------------------------------------------------
		processor.Register("scene.info", "Name, entity count and run state of the current scene.", "{}", [scene](const json&) -> CommandResult
		{
			Ref<Scene> current = scene();
			return json{
				{ "name", current->GetName() },
				{ "entityCount", current->GetEntityCount() },
				{ "running", current->IsRunning() },
				{ "paused", current->IsPaused() },
				{ "time", current->GetTime() },
				{ "frame", current->GetFrameIndex() }
			};
		});

		processor.Register("scene.get", "The complete current scene in .oscene JSON format.", "{}", [scene](const json&) -> CommandResult
		{
			return SceneSerializer(scene()).Serialize();
		});

		// Entities ----------------------------------------------------------------------------------------------------
		processor.Register("entity.list", "Lists entities (optionally only those with a component).", "{\"component\"?: string}", [scene](const json& args) -> CommandResult
		{
			Ref<Scene> current = scene();
			const ComponentInfo* filter = nullptr;
			if (args.contains("component"))
			{
				filter = ComponentRegistry::Find(args["component"].get<std::string>());
				if (!filter)
					throw CommandError("unknown component type '" + args["component"].get<std::string>() + "'");
			}

			json entities = json::array();
			for (UUID uuid : current->GetEntityOrder())
			{
				Entity entity = current->GetEntityByUUID(uuid);
				if (!filter || filter->Has(entity))
					entities.push_back(DescribeEntity(entity));
			}
			return entities;
		});

		processor.Register("entity.get", "All components of one entity.", "{\"entity\": id | name}", [scene](const json& args) -> CommandResult
		{
			return SceneSerializer::SerializeEntity(ResolveEntity(*scene(), args));
		});

		processor.Register("entity.create", "Creates an entity, optionally with components ({\"MeshRenderer\": {...}}).",
			"{\"name\"?: string, \"components\"?: object}", [scene](const json& args) -> CommandResult
		{
			Ref<Scene> current = scene();
			Entity entity = current->CreateEntity(args.value("name", std::string("Entity")));
			if (auto components = args.find("components"); components != args.end())
			{
				try
				{
					if (!components->is_object())
						throw CommandError("'components' must be an object");
					for (auto it = components->begin(); it != components->end(); ++it)
					{
						const ComponentInfo* info = ComponentRegistry::Find(it.key());
						if (!info)
							throw CommandError("unknown component type '" + it.key() + "'");
						info->Deserialize(entity, it.value());
					}
				}
				catch (...)
				{
					current->DestroyEntity(entity);
					throw;
				}
			}
			return DescribeEntity(entity);
		});

		processor.Register("entity.destroy", "Destroys an entity.", "{\"entity\": id | name}", [scene](const json& args) -> CommandResult
		{
			Ref<Scene> current = scene();
			Entity entity = ResolveEntity(*current, args);
			const uint64_t id = entity.GetUUID();
			current->DestroyEntity(entity);
			return json{ { "destroyed", id } };
		});

		processor.Register("entity.duplicate", "Duplicates an entity and all its components.", "{\"entity\": id | name}", [scene](const json& args) -> CommandResult
		{
			Ref<Scene> current = scene();
			return DescribeEntity(current->DuplicateEntity(ResolveEntity(*current, args)));
		});

		processor.Register("entity.rename", "Renames an entity.", "{\"entity\": id | name, \"newName\": string}", [scene](const json& args) -> CommandResult
		{
			Entity entity = ResolveEntity(*scene(), args);
			entity.GetComponent<TagComponent>().Tag = CommandProcessor::RequireArgument(args, "newName").get<std::string>();
			return DescribeEntity(entity);
		});

		// Components --------------------------------------------------------------------------------------------------
		processor.Register("component.types", "Lists component type names with their default values.", "{}", [](const json&) -> CommandResult
		{
			Scene scratch("Scratch");
			Entity entity = scratch.CreateEntity("Defaults");
			json types = json::object();
			for (const ComponentInfo& info : ComponentRegistry::GetAll())
			{
				info.Add(entity);
				types[info.Name] = { { "removable", info.Removable }, { "defaults", info.Serialize(entity) } };
			}
			return types;
		});

		processor.Register("component.get", "One component of an entity.", "{\"entity\": id | name, \"type\": string}", [scene](const json& args) -> CommandResult
		{
			Entity entity = ResolveEntity(*scene(), args);
			const ComponentInfo& info = RequireComponentType(args);
			if (!info.Has(entity))
				throw CommandError("entity '" + entity.GetName() + "' has no " + info.Name + " component");
			return info.Serialize(entity);
		});

		processor.Register("component.add", "Adds a component (with optional initial fields).", "{\"entity\": id | name, \"type\": string, \"data\"?: object}",
			[scene](const json& args) -> CommandResult
		{
			Entity entity = ResolveEntity(*scene(), args);
			const ComponentInfo& info = RequireComponentType(args);
			if (info.Has(entity))
				throw CommandError("entity '" + entity.GetName() + "' already has a " + info.Name + " component");
			info.Deserialize(entity, args.value("data", json::object()));
			return info.Serialize(entity);
		});

		processor.Register("component.set", "Patches fields of a component (adds it if missing).", "{\"entity\": id | name, \"type\": string, \"data\": object}",
			[scene](const json& args) -> CommandResult
		{
			Entity entity = ResolveEntity(*scene(), args);
			const ComponentInfo& info = RequireComponentType(args);
			info.Deserialize(entity, CommandProcessor::RequireArgument(args, "data"));
			return info.Serialize(entity);
		});

		processor.Register("component.remove", "Removes a component.", "{\"entity\": id | name, \"type\": string}", [scene](const json& args) -> CommandResult
		{
			Entity entity = ResolveEntity(*scene(), args);
			const ComponentInfo& info = RequireComponentType(args);
			if (!info.Removable)
				throw CommandError(info.Name + " cannot be removed");
			if (!info.Has(entity))
				throw CommandError("entity '" + entity.GetName() + "' has no " + info.Name + " component");
			info.Remove(entity);
			return DescribeEntity(entity);
		});

		// Scripting ---------------------------------------------------------------------------------------------------
		processor.Register("script.exec", "Runs Lua code in the running scene (play mode only); returns tostring of the result.",
			"{\"code\": string}", [scene](const json& args) -> CommandResult
		{
			Ref<Scene> current = scene();
			ScriptRuntime* runtime = current->GetScriptRuntime();
			if (!current->IsRunning() || !runtime)
				throw CommandError("scripts only run in play mode (use editor.play)");
			ScriptExecutionResult result = runtime->Execute(CommandProcessor::RequireArgument(args, "code").get<std::string>(), "=agent");
			if (!result.Success)
				throw CommandError(result.Error);
			return json{ { "value", result.Value } };
		});

		processor.Register("script.errors", "Errors reported by scripts in the running scene.", "{}", [scene](const json&) -> CommandResult
		{
			Ref<Scene> current = scene();
			ScriptRuntime* runtime = current->GetScriptRuntime();
			if (!runtime)
				return json{ { "count", 0 }, { "errors", json::array() } };
			return json{ { "count", runtime->GetErrorCount() }, { "errors", runtime->GetErrors() } };
		});

		// Input -------------------------------------------------------------------------------------------------------
		processor.Register("input.key", "Presses or releases a key (key names as in Lua's Key table, e.g. \"Up\", \"W\", \"Space\").",
			"{\"key\": string | code, \"down\": bool}", [](const json& args) -> CommandResult
		{
			const KeyCode key = RequireKey(args);
			Input::OnKey(key, CommandProcessor::RequireArgument(args, "down").get<bool>());
			return json{ { "key", Input::KeyToName(key) } };
		});

		processor.Register("input.tap", "Presses a key for `frames` frames (default 1), then releases it. Completes after release.",
			"{\"key\": string | code, \"frames\"?: int}", [](const json& args) -> CommandResult
		{
			const KeyCode key = RequireKey(args);
			const int frames = std::max(1, args.value("frames", 1));
			Input::OnKey(key, true);
			auto remaining = std::make_shared<int>(frames);
			return CommandResult::Deferred([key, remaining]() -> std::optional<json>
			{
				if (--(*remaining) > 0)
					return std::nullopt;
				Input::OnKey(key, false);
				return json{ { "key", Input::KeyToName(key) } };
			});
		});

		// Log ---------------------------------------------------------------------------------------------------------
		processor.Register("log.get", "Recent log messages. Pass the returned 'next' as 'since' to get only newer ones.",
			"{\"since\"?: int, \"level\"?: \"trace\"|\"info\"|\"warn\"|\"error\", \"limit\"?: int}", [](const json& args) -> CommandResult
		{
			const uint64_t since = args.value("since", uint64_t(0));
			const std::string level = args.value("level", std::string("trace"));
			const size_t limit = args.value("limit", size_t(500));

			LogLevel minimum = LogLevel::Trace;
			if (level == "info")
				minimum = LogLevel::Info;
			else if (level == "warn")
				minimum = LogLevel::Warn;
			else if (level == "error")
				minimum = LogLevel::Error;

			std::vector<LogMessage> messages = Log::GetBuffer().GetMessages(since);
			json result = json::array();
			for (const LogMessage& message : messages)
			{
				if (message.Level < minimum)
					continue;
				result.push_back({ { "index", message.Index }, { "level", Log::LevelToString(message.Level) }, { "logger", message.Logger }, { "text", message.Text } });
			}
			if (result.size() > limit)
				result.erase(result.begin(), result.begin() + static_cast<std::ptrdiff_t>(result.size() - limit));
			return json{ { "messages", result }, { "next", Log::GetBuffer().GetNextIndex() }, { "errorCount", Log::GetBuffer().GetErrorCount() } };
		});

		processor.Register("log.clear", "Clears the log buffer and error count.", "{}", [](const json&) -> CommandResult
		{
			Log::GetBuffer().Clear();
			return json{ { "cleared", true } };
		});
	}

}
