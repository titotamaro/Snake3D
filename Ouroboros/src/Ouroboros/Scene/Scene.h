#pragma once

#include "Ouroboros/Core/Base.h"
#include "Ouroboros/Core/Timestep.h"
#include "Ouroboros/Core/UUID.h"

#include <entt/entt.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Ouroboros {

	class Entity;
	class PhysicsWorld;
	class ScriptRuntime;
	class SceneAudio;

	class Scene
	{
	public:
		static constexpr float FixedTimestep = 1.0f / 60.0f;
		static constexpr int MaxFixedStepsPerFrame = 8;

		explicit Scene(const std::string& name = "Untitled");
		~Scene();

		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;

		// Deep copy of all entities and components (used to enter play mode without touching the edited scene).
		static Ref<Scene> Copy(const Ref<Scene>& other);

		Entity CreateEntity(const std::string& name = std::string());
		Entity CreateEntityWithUUID(UUID uuid, const std::string& name = std::string());
		// Destroys the entity immediately. While running, scripts receive OnDestroy and physics/audio are released first.
		void DestroyEntity(Entity entity);
		Entity DuplicateEntity(Entity entity);

		Entity FindEntityByName(std::string_view name);
		Entity GetEntityByUUID(UUID uuid);
		bool HasEntity(UUID uuid) const;
		// Entities in creation order (stable across save/load).
		const std::vector<UUID>& GetEntityOrder() const { return m_EntityOrder; }
		size_t GetEntityCount() const { return m_EntityOrder.size(); }

		Entity GetPrimaryCameraEntity();

		// Runtime (play mode)
		void OnRuntimeStart();
		void OnRuntimeStop();
		void OnUpdateRuntime(Timestep ts);

		bool IsRunning() const { return m_IsRunning; }
		bool IsPaused() const { return m_IsPaused; }
		void SetPaused(bool paused) { m_IsPaused = paused; }
		// While paused, advances the simulation by `frames` fixed steps on the next update.
		void Step(int frames = 1) { m_StepFrames = frames; }

		float GetTime() const { return m_Time; }
		float GetTimeScale() const { return m_TimeScale; }
		void SetTimeScale(float timeScale) { m_TimeScale = timeScale < 0.0f ? 0.0f : timeScale; }
		uint64_t GetFrameIndex() const { return m_FrameIndex; }

		void SetViewportSize(uint32_t width, uint32_t height);
		uint32_t GetViewportWidth() const { return m_ViewportWidth; }
		uint32_t GetViewportHeight() const { return m_ViewportHeight; }

		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }

		// A scene load requested from a script; consumed by the host application (editor or runtime).
		void RequestSceneLoad(const std::string& assetPath) { m_PendingSceneLoad = assetPath; }
		std::string ConsumePendingSceneLoad();
		// A quit requested from a script; the runtime closes the game, the editor leaves play mode.
		void RequestQuit() { m_QuitRequested = true; }
		bool ConsumeQuitRequest()
		{
			const bool requested = m_QuitRequested;
			m_QuitRequested = false;
			return requested;
		}

		PhysicsWorld* GetPhysicsWorld() const { return m_PhysicsWorld.get(); }
		ScriptRuntime* GetScriptRuntime() const { return m_ScriptRuntime.get(); }
		SceneAudio* GetAudio() const { return m_Audio.get(); }

		entt::registry& GetRegistry() { return m_Registry; }
		const entt::registry& GetRegistry() const { return m_Registry; }

	private:
		void FixedUpdate();

	private:
		std::string m_Name;
		entt::registry m_Registry;
		std::unordered_map<UUID, entt::entity> m_EntityMap;
		std::vector<UUID> m_EntityOrder;

		bool m_IsRunning = false;
		bool m_IsPaused = false;
		int m_StepFrames = 0;
		float m_Time = 0.0f;
		float m_TimeScale = 1.0f;
		float m_FixedAccumulator = 0.0f;
		uint64_t m_FrameIndex = 0;

		uint32_t m_ViewportWidth = 0;
		uint32_t m_ViewportHeight = 0;

		std::string m_PendingSceneLoad;
		bool m_QuitRequested = false;

		Scope<PhysicsWorld> m_PhysicsWorld;
		Scope<ScriptRuntime> m_ScriptRuntime;
		Scope<SceneAudio> m_Audio;

		friend class Entity;
	};

}
