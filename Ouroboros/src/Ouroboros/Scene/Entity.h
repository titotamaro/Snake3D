#pragma once

#include "Ouroboros/Core/UUID.h"
#include "Ouroboros/Scene/Components.h"
#include "Ouroboros/Scene/Scene.h"

#include <entt/entt.hpp>

namespace Ouroboros {

	// Lightweight, copyable handle to an entity inside a Scene. A default-constructed Entity is invalid.
	class Entity
	{
	public:
		Entity() = default;
		Entity(entt::entity handle, Scene* scene)
			: m_EntityHandle(handle), m_Scene(scene)
		{
		}

		template<typename T, typename... Args>
		T& AddComponent(Args&&... args)
		{
			OB_CORE_ASSERT(IsValid(), "Entity is not valid");
			OB_CORE_ASSERT(!HasComponent<T>(), "Entity already has component");
			return m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
		}

		template<typename T, typename... Args>
		T& AddOrReplaceComponent(Args&&... args)
		{
			OB_CORE_ASSERT(IsValid(), "Entity is not valid");
			return m_Scene->m_Registry.emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...);
		}

		template<typename T>
		T& GetComponent() const
		{
			OB_CORE_ASSERT(HasComponent<T>(), "Entity does not have component");
			return m_Scene->m_Registry.get<T>(m_EntityHandle);
		}

		template<typename T>
		T* TryGetComponent() const
		{
			if (!IsValid())
				return nullptr;
			return m_Scene->m_Registry.try_get<T>(m_EntityHandle);
		}

		template<typename T>
		bool HasComponent() const
		{
			return IsValid() && m_Scene->m_Registry.all_of<T>(m_EntityHandle);
		}

		template<typename T>
		void RemoveComponent()
		{
			OB_CORE_ASSERT(HasComponent<T>(), "Entity does not have component");
			m_Scene->m_Registry.remove<T>(m_EntityHandle);
		}

		bool IsValid() const { return m_Scene != nullptr && m_EntityHandle != entt::null && m_Scene->m_Registry.valid(m_EntityHandle); }
		operator bool() const { return IsValid(); }
		operator entt::entity() const { return m_EntityHandle; }
		operator uint32_t() const { return static_cast<uint32_t>(m_EntityHandle); }

		UUID GetUUID() const { return GetComponent<IDComponent>().ID; }
		const std::string& GetName() const { return GetComponent<TagComponent>().Tag; }
		TransformComponent& GetTransform() const { return GetComponent<TransformComponent>(); }

		Scene* GetScene() const { return m_Scene; }
		entt::entity GetHandle() const { return m_EntityHandle; }

		bool operator==(const Entity& other) const { return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene; }
		bool operator!=(const Entity& other) const { return !(*this == other); }

	private:
		entt::entity m_EntityHandle{ entt::null };
		Scene* m_Scene = nullptr;
	};

}
