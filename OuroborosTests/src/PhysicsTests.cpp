#include "TestUtilities.h"

#include <Ouroboros/Physics/PhysicsWorld.h>
#include <Ouroboros/Scene/Entity.h>
#include <Ouroboros/Scene/Scene.h>

using namespace Ouroboros;

namespace {

	struct PhysicsFixture
	{
		Scene SceneContext{ "Physics" };
		Scope<PhysicsWorld> World;

		Entity CreateFloor()
		{
			Entity floor = SceneContext.CreateEntity("Floor");
			floor.GetTransform().Scale = glm::vec3(20.0f, 1.0f, 20.0f);
			auto& box = floor.AddComponent<BoxColliderComponent>();
			box.HalfExtents = glm::vec3(0.5f);
			box.Offset = glm::vec3(0.0f, -0.5f, 0.0f);
			return floor;
		}

		Entity CreateDynamicBox(const std::string& name, const glm::vec3& position)
		{
			Entity entity = SceneContext.CreateEntity(name);
			entity.GetTransform().Translation = position;
			entity.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Dynamic;
			entity.AddComponent<BoxColliderComponent>();
			return entity;
		}

		void CreateWorld() { World = CreateScope<PhysicsWorld>(&SceneContext); }

		std::vector<ContactEvent> Simulate(int steps)
		{
			std::vector<ContactEvent> events;
			for (int i = 0; i < steps; i++)
			{
				World->Step(Scene::FixedTimestep);
				for (const ContactEvent& event : World->ConsumeContactEvents())
					events.push_back(event);
			}
			return events;
		}
	};

	bool HasEvent(const std::vector<ContactEvent>& events, ContactEvent::Type type, UUID a, UUID b, bool trigger)
	{
		for (const ContactEvent& event : events)
		{
			const bool pair = (event.EntityA == a && event.EntityB == b) || (event.EntityA == b && event.EntityB == a);
			if (pair && event.EventType == type && event.IsTrigger == trigger)
				return true;
		}
		return false;
	}

}

TEST_SUITE("Physics")
{
	TEST_CASE("Bodies are created from colliders; colliders without a RigidBody are static")
	{
		PhysicsFixture fixture;
		Entity floor = fixture.CreateFloor();
		Entity box = fixture.CreateDynamicBox("Box", glm::vec3(0.0f, 5.0f, 0.0f));
		Entity noCollider = fixture.SceneContext.CreateEntity("Body without collider");
		noCollider.AddComponent<RigidBodyComponent>();

		fixture.CreateWorld();
		CHECK(PhysicsWorld::IsInitialized());
		fixture.World->SyncBodies();
		CHECK(fixture.World->GetBodyCount() == 2);
		CHECK(fixture.World->HasBody(floor.GetUUID()));
		CHECK(fixture.World->HasBody(box.GetUUID()));
		CHECK_FALSE(fixture.World->HasBody(noCollider.GetUUID()));
	}

	TEST_CASE("Dynamic bodies fall, collide and come to rest on static bodies")
	{
		PhysicsFixture fixture;
		Entity floor = fixture.CreateFloor();
		Entity box = fixture.CreateDynamicBox("Box", glm::vec3(0.0f, 3.0f, 0.0f));
		fixture.CreateWorld();

		CHECK(fixture.World->GetGravity().y == doctest::Approx(-9.81f));
		const auto events = fixture.Simulate(180);

		const float height = box.GetTransform().Translation.y;
		CHECK(height == doctest::Approx(0.5f).epsilon(0.05));
		CHECK(glm::length(fixture.World->GetLinearVelocity(box.GetUUID())) < 0.1f);
		CHECK(HasEvent(events, ContactEvent::Type::Begin, box.GetUUID(), floor.GetUUID(), false));
	}

	TEST_CASE("Triggers report begin and end without blocking")
	{
		PhysicsFixture fixture;
		Entity zone = fixture.SceneContext.CreateEntity("Zone");
		zone.GetTransform().Translation = glm::vec3(0.0f, 0.0f, 0.0f);
		auto& trigger = zone.AddComponent<BoxColliderComponent>();
		trigger.HalfExtents = glm::vec3(1.0f);
		trigger.IsTrigger = true;

		Entity ball = fixture.SceneContext.CreateEntity("Ball");
		ball.GetTransform().Translation = glm::vec3(0.0f, 3.0f, 0.0f);
		ball.AddComponent<RigidBodyComponent>();
		ball.AddComponent<SphereColliderComponent>().Radius = 0.25f;

		fixture.CreateWorld();
		const auto events = fixture.Simulate(120);
		CHECK(HasEvent(events, ContactEvent::Type::Begin, ball.GetUUID(), zone.GetUUID(), true));
		CHECK(HasEvent(events, ContactEvent::Type::End, ball.GetUUID(), zone.GetUUID(), true));
		CHECK(ball.GetTransform().Translation.y < -2.0f); // fell straight through the trigger
	}

	TEST_CASE("Velocity, impulse, force and gravity controls")
	{
		PhysicsFixture fixture;
		Entity box = fixture.CreateDynamicBox("Box", glm::vec3(0.0f));
		fixture.CreateWorld();
		fixture.World->SetGravity(glm::vec3(0.0f));
		CHECK(fixture.World->GetGravity().y == doctest::Approx(0.0f));
		fixture.World->SyncBodies();

		fixture.World->SetLinearVelocity(box.GetUUID(), glm::vec3(2.0f, 0.0f, 0.0f));
		CHECK(fixture.World->GetLinearVelocity(box.GetUUID()).x == doctest::Approx(2.0f));
		fixture.Simulate(60);
		CHECK(box.GetTransform().Translation.x == doctest::Approx(2.0f).epsilon(0.1));

		fixture.World->SetLinearVelocity(box.GetUUID(), glm::vec3(0.0f));
		fixture.World->AddImpulse(box.GetUUID(), glm::vec3(0.0f, 1.0f, 0.0f)); // mass 1 -> 1 m/s
		fixture.Simulate(1);
		CHECK(fixture.World->GetLinearVelocity(box.GetUUID()).y == doctest::Approx(1.0f).epsilon(0.1));

		fixture.World->AddForce(box.GetUUID(), glm::vec3(0.0f, 0.0f, 60.0f));
		fixture.Simulate(1);
		CHECK(fixture.World->GetLinearVelocity(box.GetUUID()).z > 0.5f);

		fixture.World->SetAngularVelocity(box.GetUUID(), glm::vec3(0.0f, 1.0f, 0.0f));
		CHECK(fixture.World->GetAngularVelocity(box.GetUUID()).y == doctest::Approx(1.0f));
		fixture.World->AddTorque(box.GetUUID(), glm::vec3(0.0f, 1.0f, 0.0f));

		// Calls on entities without a body are ignored.
		fixture.World->AddImpulse(UUID(1), glm::vec3(1.0f));
		CHECK(fixture.World->GetLinearVelocity(UUID(1)) == glm::vec3(0.0f));
	}

	TEST_CASE("Raycasts hit the closest body")
	{
		PhysicsFixture fixture;
		Entity floor = fixture.CreateFloor();
		fixture.CreateWorld();
		fixture.World->SyncBodies();

		const auto hit = fixture.World->Raycast(glm::vec3(1.0f, 10.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f), 100.0f);
		REQUIRE(hit);
		CHECK(hit->Entity == floor.GetUUID());
		CHECK(hit->Distance == doctest::Approx(10.0f).epsilon(0.01));
		CHECK(hit->Position.y == doctest::Approx(0.0f).epsilon(0.01));
		CHECK(hit->Normal.y == doctest::Approx(1.0f).epsilon(0.01));

		CHECK_FALSE(fixture.World->Raycast(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 100.0f));
		CHECK_FALSE(fixture.World->Raycast(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 5.0f)); // too short
		CHECK_FALSE(fixture.World->Raycast(glm::vec3(0.0f), glm::vec3(0.0f), 5.0f));                           // no direction
	}

	TEST_CASE("Bodies follow component changes")
	{
		PhysicsFixture fixture;
		Entity box = fixture.CreateDynamicBox("Box", glm::vec3(0.0f, 10.0f, 0.0f));
		fixture.CreateWorld();
		fixture.World->SetGravity(glm::vec3(0.0f));
		fixture.World->SyncBodies();
		REQUIRE(fixture.World->HasBody(box.GetUUID()));

		// Teleporting through the transform moves the body.
		box.GetTransform().Translation = glm::vec3(5.0f, 0.0f, 0.0f);
		fixture.Simulate(1);
		CHECK(box.GetTransform().Translation.x == doctest::Approx(5.0f));

		// Changing collider settings rebuilds the body (still present).
		box.GetComponent<BoxColliderComponent>().HalfExtents = glm::vec3(2.0f);
		fixture.Simulate(1);
		CHECK(fixture.World->HasBody(box.GetUUID()));
		auto hit = fixture.World->Raycast(glm::vec3(5.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 20.0f);
		REQUIRE(hit);
		CHECK(hit->Position.y == doctest::Approx(2.0f).epsilon(0.02));

		// Kinematic bodies are moved by their transform.
		box.GetComponent<RigidBodyComponent>().Type = RigidBodyType::Kinematic;
		fixture.Simulate(1);
		box.GetTransform().Translation = glm::vec3(5.0f, 3.0f, 0.0f);
		fixture.Simulate(2);
		hit = fixture.World->Raycast(glm::vec3(5.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 20.0f);
		REQUIRE(hit);
		CHECK(hit->Position.y == doctest::Approx(5.0f).epsilon(0.05));

		// Removing the collider removes the body; destroying the entity too.
		box.RemoveComponent<BoxColliderComponent>();
		fixture.Simulate(1);
		CHECK_FALSE(fixture.World->HasBody(box.GetUUID()));

		Entity other = fixture.CreateDynamicBox("Other", glm::vec3(0.0f));
		fixture.World->SyncBodies();
		CHECK(fixture.World->HasBody(other.GetUUID()));
		fixture.World->OnEntityDestroyed(other);
		CHECK_FALSE(fixture.World->HasBody(other.GetUUID()));
	}

	TEST_CASE("Fixed rotation keeps dynamic bodies upright")
	{
		PhysicsFixture fixture;
		fixture.CreateFloor();
		Entity box = fixture.CreateDynamicBox("Box", glm::vec3(0.0f, 2.0f, 0.0f));
		box.GetComponent<RigidBodyComponent>().FixedRotation = true;
		fixture.CreateWorld();
		fixture.World->SyncBodies();
		fixture.World->SetAngularVelocity(box.GetUUID(), glm::vec3(5.0f, 5.0f, 5.0f));
		fixture.Simulate(30);
		CHECK(glm::length(box.GetTransform().Rotation) < 1e-3f);
	}

	TEST_CASE("Scene runtime drives physics and destroys bodies with entities")
	{
		Scene scene("Runtime Physics");
		Entity box = scene.CreateEntity("Box");
		box.GetTransform().Translation = glm::vec3(0.0f, 5.0f, 0.0f);
		box.AddComponent<RigidBodyComponent>();
		box.AddComponent<BoxColliderComponent>();

		scene.OnRuntimeStart();
		REQUIRE(scene.GetPhysicsWorld()->HasBody(box.GetUUID()));
		for (int i = 0; i < 30; i++)
			scene.OnUpdateRuntime(Scene::FixedTimestep);
		CHECK(box.GetTransform().Translation.y < 5.0f);

		const UUID uuid = box.GetUUID();
		scene.DestroyEntity(box);
		CHECK_FALSE(scene.GetPhysicsWorld()->HasBody(uuid));
		scene.OnRuntimeStop();
	}
}
