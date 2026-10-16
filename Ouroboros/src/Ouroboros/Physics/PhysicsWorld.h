#pragma once

#include "Ouroboros/Core/Base.h"
#include "Ouroboros/Core/UUID.h"

#include <glm/glm.hpp>

#include <optional>
#include <vector>

namespace Ouroboros {

	class Entity;
	class Scene;

	struct ContactEvent
	{
		enum class Type : uint8_t
		{
			Begin,
			End
		};

		Type EventType = Type::Begin;
		UUID EntityA = 0;
		UUID EntityB = 0;
		bool IsTrigger = false; // at least one of the bodies is a trigger (sensor)
	};

	struct RaycastHit
	{
		UUID Entity = 0;
		glm::vec3 Position{ 0.0f };
		glm::vec3 Normal{ 0.0f };
		float Distance = 0.0f;
	};

	// Jolt-backed physics simulation for a running scene.
	//
	// Bodies are derived from components: an entity with a BoxCollider and/or SphereCollider gets a body; its
	// RigidBody (if any) decides the motion type, otherwise the body is static. The world keeps bodies in sync with
	// the ECS every step: new/removed components create/destroy bodies, changed collider or rigid body settings rebuild
	// the body, transforms edited by scripts teleport bodies, and simulated dynamic bodies write their transforms back.
	class PhysicsWorld
	{
	public:
		explicit PhysicsWorld(Scene* scene);
		~PhysicsWorld();

		PhysicsWorld(const PhysicsWorld&) = delete;
		PhysicsWorld& operator=(const PhysicsWorld&) = delete;

		// Creates, rebuilds and removes bodies so they match the scene's components.
		void SyncBodies();
		// Advances the simulation by `deltaTime` seconds (one fixed step).
		void Step(float deltaTime);
		void OnEntityDestroyed(Entity entity);

		std::vector<ContactEvent> ConsumeContactEvents();

		glm::vec3 GetGravity() const;
		void SetGravity(const glm::vec3& gravity);

		std::optional<RaycastHit> Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const;

		bool HasBody(UUID entity) const;
		size_t GetBodyCount() const;

		void AddForce(UUID entity, const glm::vec3& force);
		void AddImpulse(UUID entity, const glm::vec3& impulse);
		void AddTorque(UUID entity, const glm::vec3& torque);
		void SetLinearVelocity(UUID entity, const glm::vec3& velocity);
		glm::vec3 GetLinearVelocity(UUID entity) const;
		void SetAngularVelocity(UUID entity, const glm::vec3& velocity);
		glm::vec3 GetAngularVelocity(UUID entity) const;

		// Global Jolt state is reference counted by worlds; these are exposed for tests.
		static bool IsInitialized();

	private:
		struct Impl;
		Scope<Impl> m_Impl;
	};

}
