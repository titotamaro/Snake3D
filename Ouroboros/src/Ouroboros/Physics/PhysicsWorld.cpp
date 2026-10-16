#include "obpch.h"
#include "Ouroboros/Physics/PhysicsWorld.h"

#include "Ouroboros/Scene/Entity.h"
#include "Ouroboros/Scene/Scene.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <cstdarg>
#include <thread>

namespace Ouroboros {

	namespace {

		// --------------------------------------------------------------------------------------------------------
		// Global Jolt state (reference counted by PhysicsWorld instances)

		std::mutex s_GlobalMutex;
		uint32_t s_WorldCount = 0;

		void JoltTrace(const char* format, ...)
		{
			char buffer[1024];
			va_list arguments;
			va_start(arguments, format);
			std::vsnprintf(buffer, sizeof(buffer), format, arguments);
			va_end(arguments);
			OB_CORE_TRACE("Jolt: {}", buffer);
		}

#ifdef JPH_ENABLE_ASSERTS
		bool JoltAssertFailed(const char* expression, const char* message, const char* file, JPH::uint line)
		{
			OB_CORE_ERROR("Jolt assertion '{}' failed at {}:{} {}", expression, file, line, message ? message : "");
			return false; // do not break into the debugger; Jolt continues
		}
#endif

		void AcquireJolt()
		{
			std::scoped_lock lock(s_GlobalMutex);
			if (s_WorldCount++ > 0)
				return;

			JPH::RegisterDefaultAllocator();
			JPH::Trace = JoltTrace;
			JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailed;)
			JPH::Factory::sInstance = new JPH::Factory();
			JPH::RegisterTypes();
		}

		void ReleaseJolt()
		{
			std::scoped_lock lock(s_GlobalMutex);
			if (--s_WorldCount > 0)
				return;

			JPH::UnregisterTypes();
			delete JPH::Factory::sInstance;
			JPH::Factory::sInstance = nullptr;
		}

		// --------------------------------------------------------------------------------------------------------
		// Layers

		namespace Layers {
			constexpr JPH::ObjectLayer NonMoving = 0;
			constexpr JPH::ObjectLayer Moving = 1;
			constexpr JPH::ObjectLayer Count = 2;
		}

		namespace BroadPhaseLayers {
			constexpr JPH::BroadPhaseLayer NonMoving(0);
			constexpr JPH::BroadPhaseLayer Moving(1);
			constexpr JPH::uint Count = 2;
		}

		class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer layer1, JPH::ObjectLayer layer2) const override
			{
				if (layer1 == Layers::NonMoving)
					return layer2 == Layers::Moving;
				return true;
			}
		};

		class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
		{
		public:
			JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::Count; }

			JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
			{
				return layer == Layers::NonMoving ? BroadPhaseLayers::NonMoving : BroadPhaseLayers::Moving;
			}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
			{
				return layer == BroadPhaseLayers::NonMoving ? "NonMoving" : "Moving";
			}
#endif
		};

		class ObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override
			{
				if (layer == Layers::NonMoving)
					return broadPhaseLayer == BroadPhaseLayers::Moving;
				return true;
			}
		};

		// --------------------------------------------------------------------------------------------------------
		// Conversions

		JPH::Vec3 ToJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
		JPH::Quat ToJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
		glm::vec3 FromJolt(const JPH::Vec3& v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
		glm::quat FromJolt(const JPH::Quat& q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

		JPH::EMotionType ToJolt(RigidBodyType type)
		{
			switch (type)
			{
				case RigidBodyType::Static:
					return JPH::EMotionType::Static;
				case RigidBodyType::Dynamic:
					return JPH::EMotionType::Dynamic;
				case RigidBodyType::Kinematic:
					return JPH::EMotionType::Kinematic;
			}
			return JPH::EMotionType::Static;
		}

		uint64_t MakePairKey(uint32_t a, uint32_t b)
		{
			if (a > b)
				std::swap(a, b);
			return (static_cast<uint64_t>(a) << 32) | b;
		}

		// Everything that, when changed, requires the body to be recreated.
		struct BodySignature
		{
			bool HasRigidBody = false;
			RigidBodyComponent RigidBody;
			bool HasBox = false;
			BoxColliderComponent Box;
			bool HasSphere = false;
			SphereColliderComponent Sphere;
			glm::vec3 Scale{ 1.0f };

			bool operator==(const BodySignature& other) const
			{
				auto rigidBodyEqual = [](const RigidBodyComponent& a, const RigidBodyComponent& b)
				{
					return a.Type == b.Type && a.Mass == b.Mass && a.LinearDamping == b.LinearDamping &&
						a.AngularDamping == b.AngularDamping && a.GravityFactor == b.GravityFactor && a.FixedRotation == b.FixedRotation;
				};
				auto boxEqual = [](const BoxColliderComponent& a, const BoxColliderComponent& b)
				{
					return a.HalfExtents == b.HalfExtents && a.Offset == b.Offset && a.Friction == b.Friction &&
						a.Restitution == b.Restitution && a.IsTrigger == b.IsTrigger;
				};
				auto sphereEqual = [](const SphereColliderComponent& a, const SphereColliderComponent& b)
				{
					return a.Radius == b.Radius && a.Offset == b.Offset && a.Friction == b.Friction &&
						a.Restitution == b.Restitution && a.IsTrigger == b.IsTrigger;
				};

				return HasRigidBody == other.HasRigidBody && (!HasRigidBody || rigidBodyEqual(RigidBody, other.RigidBody)) &&
					HasBox == other.HasBox && (!HasBox || boxEqual(Box, other.Box)) &&
					HasSphere == other.HasSphere && (!HasSphere || sphereEqual(Sphere, other.Sphere)) &&
					Scale == other.Scale;
			}
		};

		BodySignature MakeSignature(Entity entity)
		{
			BodySignature signature;
			if (const auto* rigidBody = entity.TryGetComponent<RigidBodyComponent>())
			{
				signature.HasRigidBody = true;
				signature.RigidBody = *rigidBody;
			}
			if (const auto* box = entity.TryGetComponent<BoxColliderComponent>())
			{
				signature.HasBox = true;
				signature.Box = *box;
			}
			if (const auto* sphere = entity.TryGetComponent<SphereColliderComponent>())
			{
				signature.HasSphere = true;
				signature.Sphere = *sphere;
			}
			signature.Scale = entity.GetTransform().Scale;
			return signature;
		}

		struct BodyRecord
		{
			JPH::BodyID ID;
			BodySignature Signature;
			glm::vec3 LastPosition{ 0.0f };
			glm::quat LastRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		};

		constexpr float TransformEpsilon = 1e-5f;

		bool NearlyEqual(const glm::vec3& a, const glm::vec3& b)
		{
			return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(TransformEpsilon)));
		}

		bool NearlyEqual(const glm::quat& a, const glm::quat& b)
		{
			return glm::abs(glm::dot(a, b)) > 1.0f - TransformEpsilon;
		}

	}

	// ----------------------------------------------------------------------------------------------------------------

	class PhysicsContactListener final : public JPH::ContactListener
	{
	public:
		explicit PhysicsContactListener(const std::unordered_map<uint32_t, UUID>& bodyToEntity)
			: m_BodyToEntity(bodyToEntity)
		{
		}

		void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold&, JPH::ContactSettings&) override
		{
			const uint32_t id1 = body1.GetID().GetIndexAndSequenceNumber();
			const uint32_t id2 = body2.GetID().GetIndexAndSequenceNumber();
			const bool isTrigger = body1.IsSensor() || body2.IsSensor();

			std::scoped_lock lock(m_Mutex);
			const uint64_t key = MakePairKey(id1, id2);
			ActivePair& pair = m_ActivePairs[key];
			if (pair.Count++ == 0)
			{
				pair.IsTrigger = isTrigger;
				PushEvent(ContactEvent::Type::Begin, id1, id2, isTrigger);
			}
		}

		void OnContactRemoved(const JPH::SubShapeIDPair& subShapePair) override
		{
			const uint32_t id1 = subShapePair.GetBody1ID().GetIndexAndSequenceNumber();
			const uint32_t id2 = subShapePair.GetBody2ID().GetIndexAndSequenceNumber();

			std::scoped_lock lock(m_Mutex);
			const uint64_t key = MakePairKey(id1, id2);
			auto it = m_ActivePairs.find(key);
			if (it == m_ActivePairs.end())
				return;
			if (--it->second.Count == 0)
			{
				PushEvent(ContactEvent::Type::End, id1, id2, it->second.IsTrigger);
				m_ActivePairs.erase(it);
			}
		}

		std::vector<ContactEvent> Consume()
		{
			std::scoped_lock lock(m_Mutex);
			std::vector<ContactEvent> events = std::move(m_Events);
			m_Events.clear();
			return events;
		}

		// Called (outside of a simulation step) when a body is removed so stale pairs do not leak.
		void ForgetBody(uint32_t bodyID)
		{
			std::scoped_lock lock(m_Mutex);
			for (auto it = m_ActivePairs.begin(); it != m_ActivePairs.end();)
			{
				const uint32_t a = static_cast<uint32_t>(it->first >> 32);
				const uint32_t b = static_cast<uint32_t>(it->first & 0xFFFFFFFFu);
				if (a == bodyID || b == bodyID)
					it = m_ActivePairs.erase(it);
				else
					++it;
			}
		}

	private:
		struct ActivePair
		{
			uint32_t Count = 0;
			bool IsTrigger = false;
		};

		void PushEvent(ContactEvent::Type type, uint32_t id1, uint32_t id2, bool isTrigger)
		{
			// The body map is not modified during a simulation step, so it can be read from worker threads.
			auto a = m_BodyToEntity.find(id1);
			auto b = m_BodyToEntity.find(id2);
			if (a == m_BodyToEntity.end() || b == m_BodyToEntity.end())
				return;
			m_Events.push_back({ type, a->second, b->second, isTrigger });
		}

		const std::unordered_map<uint32_t, UUID>& m_BodyToEntity;
		std::mutex m_Mutex;
		std::unordered_map<uint64_t, ActivePair> m_ActivePairs;
		std::vector<ContactEvent> m_Events;
	};

	// ----------------------------------------------------------------------------------------------------------------

	struct PhysicsWorld::Impl
	{
		Scene* SceneContext = nullptr;

		BroadPhaseLayerInterface BroadPhaseLayers;
		ObjectVsBroadPhaseLayerFilter ObjectVsBroadPhaseFilter;
		ObjectLayerPairFilter ObjectPairFilter;

		Scope<JPH::TempAllocatorImpl> TempAllocator;
		Scope<JPH::JobSystemThreadPool> JobSystem;
		Scope<JPH::PhysicsSystem> System;

		std::unordered_map<UUID, BodyRecord> Bodies;
		std::unordered_map<uint32_t, UUID> BodyToEntity;
		std::unordered_set<UUID> WarnedEntities;
		Scope<PhysicsContactListener> ContactListener;

		JPH::BodyInterface& GetBodyInterface() { return System->GetBodyInterface(); }

		JPH::BodyID FindBody(UUID entity) const
		{
			auto it = Bodies.find(entity);
			return it != Bodies.end() ? it->second.ID : JPH::BodyID();
		}

		JPH::RefConst<JPH::Shape> CreateShape(Entity entity, const BodySignature& signature)
		{
			const glm::vec3 scale = glm::abs(signature.Scale);
			std::vector<std::pair<glm::vec3, JPH::RefConst<JPH::Shape>>> shapes;

			if (signature.HasBox)
			{
				const glm::vec3 halfExtents = glm::max(signature.Box.HalfExtents * scale, glm::vec3(0.001f));
				const float convexRadius = std::min(JPH::cDefaultConvexRadius, 0.5f * std::min({ halfExtents.x, halfExtents.y, halfExtents.z }));
				shapes.emplace_back(signature.Box.Offset * scale, new JPH::BoxShape(ToJolt(halfExtents), convexRadius));
			}
			if (signature.HasSphere)
			{
				const float radius = std::max(signature.Sphere.Radius * std::max({ scale.x, scale.y, scale.z }), 0.001f);
				shapes.emplace_back(signature.Sphere.Offset * scale, new JPH::SphereShape(radius));
			}

			if (shapes.size() == 1)
			{
				const auto& [offset, shape] = shapes.front();
				if (glm::length(offset) < TransformEpsilon)
					return shape;

				JPH::RotatedTranslatedShapeSettings settings(ToJolt(offset), JPH::Quat::sIdentity(), shape);
				JPH::ShapeSettings::ShapeResult result = settings.Create();
				if (result.HasError())
				{
					OB_CORE_ERROR("Physics: could not create shape for '{}': {}", entity.GetName(), result.GetError().c_str());
					return nullptr;
				}
				return result.Get();
			}

			JPH::StaticCompoundShapeSettings compound;
			for (const auto& [offset, shape] : shapes)
				compound.AddShape(ToJolt(offset), JPH::Quat::sIdentity(), shape);
			JPH::ShapeSettings::ShapeResult result = compound.Create();
			if (result.HasError())
			{
				OB_CORE_ERROR("Physics: could not create compound shape for '{}': {}", entity.GetName(), result.GetError().c_str());
				return nullptr;
			}
			return result.Get();
		}

		void CreateBody(Entity entity, const BodySignature& signature)
		{
			JPH::RefConst<JPH::Shape> shape = CreateShape(entity, signature);
			if (!shape)
				return;

			const TransformComponent& transform = entity.GetTransform();
			const RigidBodyType type = signature.HasRigidBody ? signature.RigidBody.Type : RigidBodyType::Static;
			const JPH::EMotionType motionType = ToJolt(type);
			const JPH::ObjectLayer layer = type == RigidBodyType::Static ? Layers::NonMoving : Layers::Moving;
			const glm::quat rotation = glm::normalize(transform.GetRotationQuat());

			JPH::BodyCreationSettings settings(shape, JPH::RVec3(ToJolt(transform.Translation)), ToJolt(rotation), motionType, layer);
			settings.mUserData = static_cast<uint64_t>(entity.GetUUID());

			const bool isTrigger = (signature.HasBox && signature.Box.IsTrigger) || (signature.HasSphere && signature.Sphere.IsTrigger);
			settings.mIsSensor = isTrigger;
			settings.mFriction = signature.HasBox ? signature.Box.Friction : signature.Sphere.Friction;
			settings.mRestitution = signature.HasBox ? signature.Box.Restitution : signature.Sphere.Restitution;

			if (signature.HasRigidBody)
			{
				const RigidBodyComponent& rigidBody = signature.RigidBody;
				settings.mLinearDamping = rigidBody.LinearDamping;
				settings.mAngularDamping = rigidBody.AngularDamping;
				settings.mGravityFactor = rigidBody.GravityFactor;
				if (type == RigidBodyType::Dynamic)
				{
					settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
					settings.mMassPropertiesOverride.mMass = std::max(rigidBody.Mass, 0.001f);
					if (rigidBody.FixedRotation)
						settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
				}
				if (type == RigidBodyType::Kinematic)
					settings.mCollideKinematicVsNonDynamic = true;
			}

			const JPH::EActivation activation = type == RigidBodyType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
			const JPH::BodyID bodyID = GetBodyInterface().CreateAndAddBody(settings, activation);
			if (bodyID.IsInvalid())
			{
				OB_CORE_ERROR("Physics: body limit reached, could not create body for '{}'", entity.GetName());
				return;
			}

			BodyRecord record;
			record.ID = bodyID;
			record.Signature = signature;
			record.LastPosition = transform.Translation;
			record.LastRotation = rotation;
			Bodies[entity.GetUUID()] = record;
			BodyToEntity[bodyID.GetIndexAndSequenceNumber()] = entity.GetUUID();
		}

		void RemoveBody(UUID entity)
		{
			auto it = Bodies.find(entity);
			if (it == Bodies.end())
				return;

			const JPH::BodyID bodyID = it->second.ID;
			GetBodyInterface().RemoveBody(bodyID);
			GetBodyInterface().DestroyBody(bodyID);
			BodyToEntity.erase(bodyID.GetIndexAndSequenceNumber());
			ContactListener->ForgetBody(bodyID.GetIndexAndSequenceNumber());
			Bodies.erase(it);
		}
	};

	// ----------------------------------------------------------------------------------------------------------------

	PhysicsWorld::PhysicsWorld(Scene* scene)
		: m_Impl(CreateScope<Impl>())
	{
		AcquireJolt();

		m_Impl->SceneContext = scene;
		m_Impl->TempAllocator = CreateScope<JPH::TempAllocatorImpl>(16 * 1024 * 1024);

		const int threadCount = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
		m_Impl->JobSystem = CreateScope<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::min(threadCount, 8));

		constexpr JPH::uint maxBodies = 16384;
		constexpr JPH::uint maxBodyPairs = 65536;
		constexpr JPH::uint maxContactConstraints = 16384;
		m_Impl->System = CreateScope<JPH::PhysicsSystem>();
		m_Impl->System->Init(maxBodies, 0, maxBodyPairs, maxContactConstraints,
			m_Impl->BroadPhaseLayers, m_Impl->ObjectVsBroadPhaseFilter, m_Impl->ObjectPairFilter);

		m_Impl->ContactListener = CreateScope<PhysicsContactListener>(m_Impl->BodyToEntity);
		m_Impl->System->SetContactListener(m_Impl->ContactListener.get());
	}

	PhysicsWorld::~PhysicsWorld()
	{
		std::vector<UUID> entities;
		entities.reserve(m_Impl->Bodies.size());
		for (const auto& [uuid, record] : m_Impl->Bodies)
			entities.push_back(uuid);
		for (UUID uuid : entities)
			m_Impl->RemoveBody(uuid);

		m_Impl->System.reset();
		m_Impl->JobSystem.reset();
		m_Impl->TempAllocator.reset();
		m_Impl->ContactListener.reset();

		ReleaseJolt();
	}

	bool PhysicsWorld::IsInitialized()
	{
		std::scoped_lock lock(s_GlobalMutex);
		return s_WorldCount > 0;
	}

	void PhysicsWorld::SyncBodies()
	{
		Scene& scene = *m_Impl->SceneContext;

		// Remove bodies whose entity or colliders are gone.
		std::vector<UUID> stale;
		for (const auto& [uuid, record] : m_Impl->Bodies)
		{
			Entity entity = scene.GetEntityByUUID(uuid);
			if (!entity || (!entity.HasComponent<BoxColliderComponent>() && !entity.HasComponent<SphereColliderComponent>()))
				stale.push_back(uuid);
		}
		for (UUID uuid : stale)
			m_Impl->RemoveBody(uuid);

		// Create new bodies and rebuild changed ones.
		size_t created = 0;
		for (UUID uuid : scene.GetEntityOrder())
		{
			Entity entity = scene.GetEntityByUUID(uuid);
			const bool hasCollider = entity.HasComponent<BoxColliderComponent>() || entity.HasComponent<SphereColliderComponent>();
			if (!hasCollider)
			{
				if (entity.HasComponent<RigidBodyComponent>() && m_Impl->WarnedEntities.insert(uuid).second)
					OB_CORE_WARN("Physics: entity '{}' has a RigidBody but no collider; it will not be simulated", entity.GetName());
				continue;
			}

			const BodySignature signature = MakeSignature(entity);
			auto it = m_Impl->Bodies.find(uuid);
			if (it != m_Impl->Bodies.end())
			{
				if (it->second.Signature == signature)
					continue;

				// Rebuild, preserving velocities of moving bodies.
				const JPH::BodyID oldBody = it->second.ID;
				const JPH::Vec3 linearVelocity = m_Impl->GetBodyInterface().GetLinearVelocity(oldBody);
				const JPH::Vec3 angularVelocity = m_Impl->GetBodyInterface().GetAngularVelocity(oldBody);
				m_Impl->RemoveBody(uuid);
				m_Impl->CreateBody(entity, signature);
				if (const JPH::BodyID newBody = m_Impl->FindBody(uuid); !newBody.IsInvalid() && signature.HasRigidBody && signature.RigidBody.Type == RigidBodyType::Dynamic)
					m_Impl->GetBodyInterface().SetLinearAndAngularVelocity(newBody, linearVelocity, angularVelocity);
				continue;
			}

			m_Impl->CreateBody(entity, signature);
			created++;
		}

		if (created > 16)
			m_Impl->System->OptimizeBroadPhase();
	}

	void PhysicsWorld::Step(float deltaTime)
	{
		Scene& scene = *m_Impl->SceneContext;
		JPH::BodyInterface& bodyInterface = m_Impl->GetBodyInterface();

		SyncBodies();

		// Push transforms changed outside of the simulation (scripts, editor) into Jolt.
		for (auto& [uuid, record] : m_Impl->Bodies)
		{
			Entity entity = scene.GetEntityByUUID(uuid);
			const TransformComponent& transform = entity.GetTransform();
			const glm::quat rotation = glm::normalize(transform.GetRotationQuat());
			const RigidBodyType type = record.Signature.HasRigidBody ? record.Signature.RigidBody.Type : RigidBodyType::Static;

			if (type == RigidBodyType::Kinematic)
			{
				bodyInterface.MoveKinematic(record.ID, JPH::RVec3(ToJolt(transform.Translation)), ToJolt(rotation), deltaTime);
			}
			else if (!NearlyEqual(transform.Translation, record.LastPosition) || !NearlyEqual(rotation, record.LastRotation))
			{
				const JPH::EActivation activation = type == RigidBodyType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
				bodyInterface.SetPositionAndRotation(record.ID, JPH::RVec3(ToJolt(transform.Translation)), ToJolt(rotation), activation);
			}
			record.LastPosition = transform.Translation;
			record.LastRotation = rotation;
		}

		const JPH::EPhysicsUpdateError error = m_Impl->System->Update(deltaTime, 1, m_Impl->TempAllocator.get(), m_Impl->JobSystem.get());
		if (error != JPH::EPhysicsUpdateError::None)
			OB_CORE_WARN("Physics: simulation step reported error flags {}", static_cast<uint32_t>(error));

		// Pull simulated transforms back into the ECS.
		for (auto& [uuid, record] : m_Impl->Bodies)
		{
			const RigidBodyType type = record.Signature.HasRigidBody ? record.Signature.RigidBody.Type : RigidBodyType::Static;
			if (type != RigidBodyType::Dynamic)
				continue;

			JPH::RVec3 position;
			JPH::Quat rotation;
			bodyInterface.GetPositionAndRotation(record.ID, position, rotation);

			Entity entity = scene.GetEntityByUUID(uuid);
			TransformComponent& transform = entity.GetTransform();
			transform.Translation = FromJolt(JPH::Vec3(position));
			const glm::quat newRotation = glm::normalize(FromJolt(rotation));
			if (!NearlyEqual(newRotation, record.LastRotation))
				transform.SetRotationQuat(newRotation);

			record.LastPosition = transform.Translation;
			record.LastRotation = glm::normalize(transform.GetRotationQuat());
		}
	}

	void PhysicsWorld::OnEntityDestroyed(Entity entity)
	{
		m_Impl->RemoveBody(entity.GetUUID());
		m_Impl->WarnedEntities.erase(entity.GetUUID());
	}

	std::vector<ContactEvent> PhysicsWorld::ConsumeContactEvents()
	{
		return m_Impl->ContactListener->Consume();
	}

	glm::vec3 PhysicsWorld::GetGravity() const
	{
		return FromJolt(m_Impl->System->GetGravity());
	}

	void PhysicsWorld::SetGravity(const glm::vec3& gravity)
	{
		m_Impl->System->SetGravity(ToJolt(gravity));
	}

	std::optional<RaycastHit> PhysicsWorld::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const
	{
		const float length = glm::length(direction);
		if (length < 1e-6f || maxDistance <= 0.0f)
			return std::nullopt;

		const glm::vec3 normalizedDirection = direction / length;
		const JPH::RRayCast ray(JPH::RVec3(ToJolt(origin)), ToJolt(normalizedDirection * maxDistance));
		JPH::RayCastResult result;
		if (!m_Impl->System->GetNarrowPhaseQuery().CastRay(ray, result))
			return std::nullopt;

		RaycastHit hit;
		hit.Distance = result.mFraction * maxDistance;
		hit.Position = origin + normalizedDirection * hit.Distance;

		JPH::BodyLockRead lock(m_Impl->System->GetBodyLockInterface(), result.mBodyID);
		if (lock.Succeeded())
		{
			const JPH::Body& body = lock.GetBody();
			hit.Entity = body.GetUserData();
			hit.Normal = FromJolt(body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, JPH::RVec3(ToJolt(hit.Position))));
		}
		return hit;
	}

	bool PhysicsWorld::HasBody(UUID entity) const
	{
		return m_Impl->Bodies.contains(entity);
	}

	size_t PhysicsWorld::GetBodyCount() const
	{
		return m_Impl->Bodies.size();
	}

	void PhysicsWorld::AddForce(UUID entity, const glm::vec3& force)
	{
		if (const JPH::BodyID id = m_Impl->FindBody(entity); !id.IsInvalid())
			m_Impl->GetBodyInterface().AddForce(id, ToJolt(force));
	}

	void PhysicsWorld::AddImpulse(UUID entity, const glm::vec3& impulse)
	{
		if (const JPH::BodyID id = m_Impl->FindBody(entity); !id.IsInvalid())
			m_Impl->GetBodyInterface().AddImpulse(id, ToJolt(impulse));
	}

	void PhysicsWorld::AddTorque(UUID entity, const glm::vec3& torque)
	{
		if (const JPH::BodyID id = m_Impl->FindBody(entity); !id.IsInvalid())
			m_Impl->GetBodyInterface().AddTorque(id, ToJolt(torque));
	}

	void PhysicsWorld::SetLinearVelocity(UUID entity, const glm::vec3& velocity)
	{
		if (const JPH::BodyID id = m_Impl->FindBody(entity); !id.IsInvalid())
			m_Impl->GetBodyInterface().SetLinearVelocity(id, ToJolt(velocity));
	}

	glm::vec3 PhysicsWorld::GetLinearVelocity(UUID entity) const
	{
		if (const JPH::BodyID id = m_Impl->FindBody(entity); !id.IsInvalid())
			return FromJolt(m_Impl->GetBodyInterface().GetLinearVelocity(id));
		return glm::vec3(0.0f);
	}

	void PhysicsWorld::SetAngularVelocity(UUID entity, const glm::vec3& velocity)
	{
		if (const JPH::BodyID id = m_Impl->FindBody(entity); !id.IsInvalid())
			m_Impl->GetBodyInterface().SetAngularVelocity(id, ToJolt(velocity));
	}

	glm::vec3 PhysicsWorld::GetAngularVelocity(UUID entity) const
	{
		if (const JPH::BodyID id = m_Impl->FindBody(entity); !id.IsInvalid())
			return FromJolt(m_Impl->GetBodyInterface().GetAngularVelocity(id));
		return glm::vec3(0.0f);
	}

}
