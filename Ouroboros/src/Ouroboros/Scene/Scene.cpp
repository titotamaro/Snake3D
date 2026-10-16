#include "obpch.h"
#include "Ouroboros/Scene/Scene.h"

#include "Ouroboros/Audio/SceneAudio.h"
#include "Ouroboros/Physics/PhysicsWorld.h"
#include "Ouroboros/Scene/ComponentRegistry.h"
#include "Ouroboros/Scene/Entity.h"
#include "Ouroboros/Scripting/ScriptRuntime.h"

namespace Ouroboros {

	Scene::Scene(const std::string& name)
		: m_Name(name)
	{
	}

	Scene::~Scene()
	{
		if (m_IsRunning)
			OnRuntimeStop();
	}

	Ref<Scene> Scene::Copy(const Ref<Scene>& other)
	{
		Ref<Scene> scene = CreateRef<Scene>(other->m_Name);
		scene->m_ViewportWidth = other->m_ViewportWidth;
		scene->m_ViewportHeight = other->m_ViewportHeight;

		for (UUID uuid : other->m_EntityOrder)
		{
			Entity source(other->m_EntityMap.at(uuid), other.get());
			Entity destination = scene->CreateEntityWithUUID(uuid, source.GetName());
			for (const ComponentInfo& info : ComponentRegistry::GetAll())
				info.Copy(source, destination);
		}

		return scene;
	}

	Entity Scene::CreateEntity(const std::string& name)
	{
		return CreateEntityWithUUID(UUID(), name);
	}

	Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
	{
		OB_CORE_VERIFY(static_cast<uint64_t>(uuid) != 0, "UUID 0 is reserved");
		OB_CORE_VERIFY(!m_EntityMap.contains(uuid), "An entity with this UUID already exists");

		Entity entity(m_Registry.create(), this);
		entity.AddComponent<IDComponent>(uuid);
		entity.AddComponent<TagComponent>(name.empty() ? std::string("Entity") : name);
		entity.AddComponent<TransformComponent>();

		m_EntityMap[uuid] = entity.GetHandle();
		m_EntityOrder.push_back(uuid);
		return entity;
	}

	void Scene::DestroyEntity(Entity entity)
	{
		if (!entity.IsValid() || entity.GetScene() != this)
			return;

		const UUID uuid = entity.GetUUID();

		if (m_IsRunning)
		{
			if (m_ScriptRuntime)
				m_ScriptRuntime->OnEntityDestroyed(entity);
			// The script may already have destroyed the entity from inside OnDestroy.
			if (!entity.IsValid())
				return;
			if (m_PhysicsWorld)
				m_PhysicsWorld->OnEntityDestroyed(entity);
			if (m_Audio)
				m_Audio->OnEntityDestroyed(entity);
		}

		m_EntityMap.erase(uuid);
		m_EntityOrder.erase(std::remove(m_EntityOrder.begin(), m_EntityOrder.end(), uuid), m_EntityOrder.end());
		m_Registry.destroy(entity.GetHandle());
	}

	Entity Scene::DuplicateEntity(Entity entity)
	{
		if (!entity.IsValid())
			return {};

		Entity duplicate = CreateEntity(entity.GetName());
		for (const ComponentInfo& info : ComponentRegistry::GetAll())
			info.Copy(entity, duplicate);
		return duplicate;
	}

	Entity Scene::FindEntityByName(std::string_view name)
	{
		for (UUID uuid : m_EntityOrder)
		{
			Entity entity(m_EntityMap.at(uuid), this);
			if (entity.GetName() == name)
				return entity;
		}
		return {};
	}

	Entity Scene::GetEntityByUUID(UUID uuid)
	{
		if (auto it = m_EntityMap.find(uuid); it != m_EntityMap.end())
			return Entity(it->second, this);
		return {};
	}

	bool Scene::HasEntity(UUID uuid) const
	{
		return m_EntityMap.contains(uuid);
	}

	Entity Scene::GetPrimaryCameraEntity()
	{
		Entity fallback;
		for (UUID uuid : m_EntityOrder)
		{
			Entity entity(m_EntityMap.at(uuid), this);
			if (const CameraComponent* camera = entity.TryGetComponent<CameraComponent>())
			{
				if (camera->Primary)
					return entity;
				if (!fallback)
					fallback = entity;
			}
		}
		return fallback;
	}

	void Scene::OnRuntimeStart()
	{
		if (m_IsRunning)
			return;

		OB_CORE_INFO("Scene '{}': runtime start", m_Name);
		m_IsRunning = true;
		m_IsPaused = false;
		m_Time = 0.0f;
		m_FixedAccumulator = 0.0f;
		m_FrameIndex = 0;

		m_PhysicsWorld = CreateScope<PhysicsWorld>(this);
		m_Audio = CreateScope<SceneAudio>(this);
		m_ScriptRuntime = CreateScope<ScriptRuntime>(this);

		m_PhysicsWorld->SyncBodies();
		m_Audio->Start();
		m_ScriptRuntime->Start();
	}

	void Scene::OnRuntimeStop()
	{
		if (!m_IsRunning)
			return;

		OB_CORE_INFO("Scene '{}': runtime stop", m_Name);

		if (m_ScriptRuntime)
			m_ScriptRuntime->Stop();
		if (m_Audio)
			m_Audio->Stop();

		m_ScriptRuntime.reset();
		m_Audio.reset();
		m_PhysicsWorld.reset();
		m_IsRunning = false;
	}

	void Scene::OnUpdateRuntime(Timestep ts)
	{
		if (!m_IsRunning)
			return;

		float deltaTime = ts.GetSeconds() * m_TimeScale;
		if (m_IsPaused)
		{
			if (m_StepFrames <= 0)
				return;
			deltaTime = FixedTimestep * static_cast<float>(m_StepFrames);
			m_StepFrames = 0;
		}

		// Clamp large hitches (debugger breaks, window drags) so simulations stay stable.
		deltaTime = std::min(deltaTime, FixedTimestep * static_cast<float>(MaxFixedStepsPerFrame));

		m_ScriptRuntime->Update(deltaTime);

		m_FixedAccumulator += deltaTime;
		int steps = 0;
		while (m_FixedAccumulator >= FixedTimestep && steps < MaxFixedStepsPerFrame)
		{
			FixedUpdate();
			m_FixedAccumulator -= FixedTimestep;
			steps++;
		}

		m_Audio->Update();

		m_Time += deltaTime;
		m_FrameIndex++;
	}

	void Scene::FixedUpdate()
	{
		m_ScriptRuntime->FixedUpdate(FixedTimestep);
		m_PhysicsWorld->Step(FixedTimestep);

		for (const ContactEvent& contact : m_PhysicsWorld->ConsumeContactEvents())
			m_ScriptRuntime->DispatchContact(contact);
	}

	void Scene::SetViewportSize(uint32_t width, uint32_t height)
	{
		m_ViewportWidth = width;
		m_ViewportHeight = height;
	}

	std::string Scene::ConsumePendingSceneLoad()
	{
		std::string path = std::move(m_PendingSceneLoad);
		m_PendingSceneLoad.clear();
		return path;
	}

}
