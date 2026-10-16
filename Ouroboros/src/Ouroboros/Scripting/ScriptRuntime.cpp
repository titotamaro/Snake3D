#include "obpch.h"
#include "Ouroboros/Scripting/ScriptRuntime.h"

#include "Ouroboros/Core/FileSystem.h"
#include "Ouroboros/Physics/PhysicsWorld.h"
#include "Ouroboros/Project/Project.h"
#include "Ouroboros/Scene/Entity.h"
#include "Ouroboros/Scripting/ScriptGlue.h"

namespace Ouroboros {

	namespace {

		constexpr size_t MaxStoredErrors = 128;

		sol::protected_function GetCallback(const sol::table& classTable, const char* name)
		{
			sol::object value = classTable[name];
			if (value.get_type() == sol::type::function)
				return value.as<sol::protected_function>();
			return sol::protected_function();
		}

	}

	ScriptRuntime::ScriptRuntime(Scene* scene)
		: m_Scene(scene), m_Random(std::random_device{}())
	{
		// The `io`, `os` and `debug` libraries are intentionally not opened: scripts are sandboxed to the engine API.
		m_State.open_libraries(sol::lib::base, sol::lib::package, sol::lib::coroutine, sol::lib::string,
			sol::lib::table, sol::lib::math, sol::lib::utf8);

		std::string packagePath;
		if (Ref<Project> project = Project::GetActive())
		{
			const std::string assets = project->GetAssetDirectory().generic_string();
			packagePath = assets + "/?.lua;" + assets + "/Scripts/?.lua";
		}
		m_State["package"]["path"] = packagePath;
		m_State["package"]["cpath"] = "";

		// Keep scripts away from native code and the file system: only Lua modules from the project can be required.
		m_State["package"]["loadlib"] = sol::lua_nil;
		m_State["dofile"] = sol::lua_nil;
		m_State["loadfile"] = sol::lua_nil;
		sol::table searchers = m_State["package"]["searchers"];
		searchers[3] = sol::lua_nil; // C library searcher
		searchers[4] = sol::lua_nil; // all-in-one C searcher

		ScriptGlue::Register(*this);
	}

	ScriptRuntime::~ScriptRuntime()
	{
		// Instances and classes reference the Lua state; release them before it is closed.
		m_Instances.clear();
		m_Classes.clear();
	}

	void ScriptRuntime::ReportError(const std::string& message)
	{
		OB_ERROR("[Script] {}", message);
		m_ErrorCount++;
		if (m_Errors.size() < MaxStoredErrors)
			m_Errors.push_back(message);
	}

	ScriptRuntime::ScriptClass* ScriptRuntime::LoadClass(const std::string& scriptPath)
	{
		if (auto it = m_Classes.find(scriptPath); it != m_Classes.end())
			return it->second.Valid ? &it->second : nullptr;

		ScriptClass& scriptClass = m_Classes[scriptPath];

		const std::filesystem::path fullPath = Project::ResolveAssetPath(scriptPath);
		std::optional<std::string> source = FileSystem::ReadTextFile(fullPath);
		if (!source)
		{
			ReportError("Could not read script '" + scriptPath + "' (" + fullPath.string() + ")");
			return nullptr;
		}

		sol::load_result chunk = m_State.load(*source, "@" + scriptPath);
		if (!chunk.valid())
		{
			sol::error error = chunk;
			ReportError("Syntax error in '" + scriptPath + "': " + error.what());
			return nullptr;
		}

		sol::protected_function function = chunk;
		sol::protected_function_result result = function();
		if (!result.valid())
		{
			sol::error error = result;
			ReportError("Error while loading '" + scriptPath + "': " + error.what());
			return nullptr;
		}

		sol::object value = result;
		if (value.get_type() != sol::type::table)
		{
			ReportError("Script '" + scriptPath + "' must return a table (its class)");
			return nullptr;
		}

		scriptClass.Table = value.as<sol::table>();
		scriptClass.Valid = true;
		return &scriptClass;
	}

	bool ScriptRuntime::InstantiateEntity(Entity entity)
	{
		const UUID uuid = entity.GetUUID();
		if (m_Instances.contains(uuid))
			return true;

		const ScriptComponent* component = entity.TryGetComponent<ScriptComponent>();
		if (!component || component->Script.empty())
			return false;

		const std::string scriptPath = component->Script;
		ScriptClass* scriptClass = LoadClass(scriptPath);
		if (!scriptClass)
		{
			m_FailedInstances[uuid] = scriptPath;
			return false;
		}

		sol::table self = m_State.create_table();
		sol::table metatable = m_State.create_table();
		metatable[sol::meta_function::index] = scriptClass->Table;
		self[sol::metatable_key] = metatable;
		self["Entity"] = LuaEntity{ m_Scene, uuid };

		// Instance properties: defaults from the class `Properties` table, then per-entity overrides from the component.
		// Values are deep-copied through JSON so instances never share tables or vectors.
		sol::object properties = scriptClass->Table["Properties"];
		if (properties.get_type() == sol::type::table)
		{
			for (const auto& [key, value] : properties.as<sol::table>())
			{
				if (key.get_type() == sol::type::string)
					self[key] = ScriptGlue::JsonToLua(m_State, ScriptGlue::LuaToJson(value));
			}
		}
		for (auto field = component->Fields.begin(); field != component->Fields.end(); ++field)
			self[field.key()] = ScriptGlue::JsonToLua(m_State, field.value());

		ScriptInstance instance;
		instance.ScriptPath = scriptPath;
		instance.Self = self;
		instance.OnCreate = GetCallback(scriptClass->Table, "OnCreate");
		instance.OnUpdate = GetCallback(scriptClass->Table, "OnUpdate");
		instance.OnFixedUpdate = GetCallback(scriptClass->Table, "OnFixedUpdate");
		instance.OnDestroy = GetCallback(scriptClass->Table, "OnDestroy");
		instance.OnCollisionBegin = GetCallback(scriptClass->Table, "OnCollisionBegin");
		instance.OnCollisionEnd = GetCallback(scriptClass->Table, "OnCollisionEnd");
		instance.OnTriggerBegin = GetCallback(scriptClass->Table, "OnTriggerBegin");
		instance.OnTriggerEnd = GetCallback(scriptClass->Table, "OnTriggerEnd");

		m_Instances[uuid] = std::move(instance);
		m_InstanceOrder.push_back(uuid);
		m_FailedInstances.erase(uuid);

		Call(uuid, &ScriptInstance::OnCreate, "OnCreate");
		return true;
	}

	template<typename... Args>
	void ScriptRuntime::Call(UUID uuid, sol::protected_function ScriptInstance::*callback, const char* callbackName, Args&&... args)
	{
		auto it = m_Instances.find(uuid);
		if (it == m_Instances.end())
			return;

		// Copy what is needed: the callback may create or destroy entities (and therefore instances).
		sol::protected_function function = it->second.*callback;
		if (!function.valid())
			return;
		sol::table self = it->second.Self;
		const std::string scriptPath = it->second.ScriptPath;

		sol::protected_function_result result = function(self, std::forward<Args>(args)...);
		if (result.valid())
			return;

		sol::error error = result;
		ReportError(scriptPath + ": " + callbackName + " failed: " + error.what());

		// Disable the failing callback so a broken script does not flood the log every frame.
		if (auto again = m_Instances.find(uuid); again != m_Instances.end())
			again->second.*callback = sol::protected_function();
	}

	void ScriptRuntime::ReleaseInstance(UUID uuid, bool callOnDestroy)
	{
		auto it = m_Instances.find(uuid);
		if (it == m_Instances.end())
			return;

		// Remove first so that re-entrant destruction from OnDestroy is a no-op.
		ScriptInstance instance = std::move(it->second);
		m_Instances.erase(it);
		m_InstanceOrder.erase(std::remove(m_InstanceOrder.begin(), m_InstanceOrder.end(), uuid), m_InstanceOrder.end());

		if (callOnDestroy && instance.OnDestroy.valid())
		{
			sol::protected_function_result result = instance.OnDestroy(instance.Self);
			if (!result.valid())
			{
				sol::error error = result;
				ReportError(instance.ScriptPath + ": OnDestroy failed: " + error.what());
			}
		}
	}

	void ScriptRuntime::SyncInstances()
	{
		// Release instances whose entity or ScriptComponent is gone (or whose script changed).
		std::vector<UUID> order = m_InstanceOrder;
		for (UUID uuid : order)
		{
			auto it = m_Instances.find(uuid);
			if (it == m_Instances.end())
				continue;

			Entity entity = m_Scene->GetEntityByUUID(uuid);
			const ScriptComponent* component = entity ? entity.TryGetComponent<ScriptComponent>() : nullptr;
			if (!component || component->Script != it->second.ScriptPath)
				ReleaseInstance(uuid, entity.IsValid());
		}

		// Instantiate new ScriptComponents.
		std::vector<UUID> entities = m_Scene->GetEntityOrder();
		for (UUID uuid : entities)
		{
			if (m_Instances.contains(uuid))
				continue;

			Entity entity = m_Scene->GetEntityByUUID(uuid);
			if (!entity)
				continue;
			const ScriptComponent* component = entity.TryGetComponent<ScriptComponent>();
			if (!component || component->Script.empty())
				continue;

			// Do not retry scripts that failed to load until the path changes.
			if (auto failed = m_FailedInstances.find(uuid); failed != m_FailedInstances.end() && failed->second == component->Script)
				continue;

			InstantiateEntity(entity);
		}
	}

	void ScriptRuntime::Start()
	{
		m_Started = true;
		SyncInstances();
	}

	void ScriptRuntime::Update(float deltaTime)
	{
		m_DeltaTime = deltaTime;
		m_UIText.clear();

		SyncInstances();

		std::vector<UUID> order = m_InstanceOrder;
		for (UUID uuid : order)
			Call(uuid, &ScriptInstance::OnUpdate, "OnUpdate", deltaTime);
	}

	void ScriptRuntime::FixedUpdate(float deltaTime)
	{
		std::vector<UUID> order = m_InstanceOrder;
		for (UUID uuid : order)
			Call(uuid, &ScriptInstance::OnFixedUpdate, "OnFixedUpdate", deltaTime);
	}

	void ScriptRuntime::Stop()
	{
		std::vector<UUID> order = m_InstanceOrder;
		for (UUID uuid : order)
			ReleaseInstance(uuid, true);
		m_Started = false;
	}

	void ScriptRuntime::OnEntityDestroyed(Entity entity)
	{
		ReleaseInstance(entity.GetUUID(), true);
		m_FailedInstances.erase(entity.GetUUID());
	}

	void ScriptRuntime::DispatchContact(const ContactEvent& contact)
	{
		const bool begin = contact.EventType == ContactEvent::Type::Begin;
		sol::protected_function ScriptInstance::*callback = nullptr;
		const char* callbackName = nullptr;
		if (contact.IsTrigger)
		{
			callback = begin ? &ScriptInstance::OnTriggerBegin : &ScriptInstance::OnTriggerEnd;
			callbackName = begin ? "OnTriggerBegin" : "OnTriggerEnd";
		}
		else
		{
			callback = begin ? &ScriptInstance::OnCollisionBegin : &ScriptInstance::OnCollisionEnd;
			callbackName = begin ? "OnCollisionBegin" : "OnCollisionEnd";
		}

		Call(contact.EntityA, callback, callbackName, LuaEntity{ m_Scene, contact.EntityB });
		Call(contact.EntityB, callback, callbackName, LuaEntity{ m_Scene, contact.EntityA });
	}

	sol::object ScriptRuntime::GetInstance(UUID entity)
	{
		if (auto it = m_Instances.find(entity); it != m_Instances.end())
			return it->second.Self;
		return sol::make_object(m_State, sol::lua_nil);
	}

	ScriptExecutionResult ScriptRuntime::Execute(const std::string& code, const std::string& chunkName)
	{
		ScriptExecutionResult executionResult;

		sol::load_result chunk = m_State.load(code, chunkName);
		if (!chunk.valid())
		{
			sol::error error = chunk;
			executionResult.Error = error.what();
			return executionResult;
		}

		sol::protected_function function = chunk;
		sol::protected_function_result result = function();
		if (!result.valid())
		{
			sol::error error = result;
			executionResult.Error = error.what();
			return executionResult;
		}

		executionResult.Success = true;
		if (result.return_count() > 0)
		{
			sol::object value = result.get<sol::object>(0);
			sol::protected_function toString = m_State["tostring"];
			sol::protected_function_result text = toString(value);
			if (text.valid())
				executionResult.Value = text.get<std::string>();
		}
		return executionResult;
	}

}
