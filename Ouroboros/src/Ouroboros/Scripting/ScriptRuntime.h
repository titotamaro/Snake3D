#pragma once

#include "Ouroboros/Core/Base.h"
#include "Ouroboros/Core/UUID.h"

#include <glm/glm.hpp>
#include <sol/sol.hpp>

#include <filesystem>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace Ouroboros {

	class Entity;
	class Scene;
	struct ContactEvent;

	enum class UIAnchor : uint8_t
	{
		TopLeft = 0,
		Top,
		TopRight,
		Left,
		Center,
		Right,
		BottomLeft,
		Bottom,
		BottomRight
	};

	// A text label queued by a script for the current frame (drawn by the GameHUD on top of the viewport).
	struct UITextElement
	{
		std::string Text;
		glm::vec2 Position{ 0.0f }; // pixels, relative to the anchor; +Y is down
		float Size = 24.0f;         // pixels
		glm::vec4 Color{ 1.0f };
		UIAnchor Anchor = UIAnchor::TopLeft;
	};

	struct ScriptExecutionResult
	{
		bool Success = false;
		std::string Error;
		std::string Value; // tostring() of the first returned value, if any
	};

	// Lua scripting for a running scene. Each running scene owns one Lua state; every ScriptComponent gets an instance
	// table whose metatable is the class table returned by the script file. See AGENTS.md for the scripting API.
	class ScriptRuntime
	{
	public:
		explicit ScriptRuntime(Scene* scene);
		~ScriptRuntime();

		ScriptRuntime(const ScriptRuntime&) = delete;
		ScriptRuntime& operator=(const ScriptRuntime&) = delete;

		// Creates instances for every ScriptComponent (in entity order) and calls OnCreate.
		void Start();
		// Creates instances for new ScriptComponents, releases removed ones and calls OnUpdate(dt).
		void Update(float deltaTime);
		void FixedUpdate(float deltaTime);
		// Calls OnDestroy on every instance.
		void Stop();

		void OnEntityDestroyed(Entity entity);
		void DispatchContact(const ContactEvent& contact);

		// Instantiates the script on `entity` immediately (used by Entity:AddScript). Returns false on error.
		bool InstantiateEntity(Entity entity);
		// Calls OnDestroy (if requested) and removes the instance of `entity`.
		void ReleaseInstance(UUID entity, bool callOnDestroy);
		sol::object GetInstance(UUID entity);
		bool HasInstance(UUID entity) const { return m_Instances.contains(entity); }
		size_t GetInstanceCount() const { return m_Instances.size(); }

		// Runs a chunk of Lua inside this runtime (used by tests and the agent command API).
		ScriptExecutionResult Execute(const std::string& code, const std::string& chunkName = "=execute");

		uint32_t GetErrorCount() const { return m_ErrorCount; }
		const std::vector<std::string>& GetErrors() const { return m_Errors; }
		void ReportError(const std::string& message);

		const std::vector<UITextElement>& GetUIText() const { return m_UIText; }
		void AddUIText(UITextElement element) { m_UIText.push_back(std::move(element)); }

		float GetDeltaTime() const { return m_DeltaTime; }
		std::mt19937& GetRandomEngine() { return m_Random; }

		Scene* GetScene() const { return m_Scene; }
		sol::state& GetState() { return m_State; }

	private:
		struct ScriptClass
		{
			sol::table Table;
			bool Valid = false;
		};

		struct ScriptInstance
		{
			std::string ScriptPath;
			sol::table Self;
			sol::protected_function OnCreate;
			sol::protected_function OnUpdate;
			sol::protected_function OnFixedUpdate;
			sol::protected_function OnDestroy;
			sol::protected_function OnCollisionBegin;
			sol::protected_function OnCollisionEnd;
			sol::protected_function OnTriggerBegin;
			sol::protected_function OnTriggerEnd;
		};

		ScriptClass* LoadClass(const std::string& scriptPath);
		void SyncInstances();

		template<typename... Args>
		void Call(UUID uuid, sol::protected_function ScriptInstance::*callback, const char* callbackName, Args&&... args);

	private:
		Scene* m_Scene;
		sol::state m_State;
		std::unordered_map<std::string, ScriptClass> m_Classes;
		std::unordered_map<UUID, ScriptInstance> m_Instances;
		std::vector<UUID> m_InstanceOrder;
		std::unordered_map<UUID, std::string> m_FailedInstances; // entity -> script path that failed to instantiate

		std::vector<UITextElement> m_UIText;
		std::vector<std::string> m_Errors;
		uint32_t m_ErrorCount = 0;
		float m_DeltaTime = 0.0f;
		std::mt19937 m_Random;
		bool m_Started = false;
	};

}
