#include "TestUtilities.h"

#include <Ouroboros/Core/FileSystem.h>
#include <Ouroboros/Scene/ComponentRegistry.h>
#include <Ouroboros/Scene/ComponentSerialization.h>
#include <Ouroboros/Scene/Entity.h>
#include <Ouroboros/Scene/Scene.h>
#include <Ouroboros/Scene/SceneSerializer.h>

using namespace Ouroboros;
using json = nlohmann::json;

namespace {

	// Populates every component with non-default values.
	Entity CreateFullEntity(Scene& scene)
	{
		Entity entity = scene.CreateEntityWithUUID(12345, "Everything");
		auto& transform = entity.GetTransform();
		transform.Translation = { 1, 2, 3 };
		transform.Rotation = { 0.1f, 0.2f, 0.3f };
		transform.Scale = { 2, 2, 2 };

		auto& camera = entity.AddComponent<CameraComponent>();
		camera.Projection = ProjectionType::Orthographic;
		camera.OrthographicSize = 7;
		camera.Exposure = 1.5f;
		camera.Primary = false;

		auto& mesh = entity.AddComponent<MeshRendererComponent>();
		mesh.Mesh = "Meshes/Apple.gltf";
		mesh.Color = { 0.5f, 0.6f, 0.7f, 0.8f };
		mesh.Metallic = 0.3f;
		mesh.Roughness = 0.4f;
		mesh.Emissive = { 1, 0, 0 };
		mesh.CastShadows = false;

		auto& sun = entity.AddComponent<DirectionalLightComponent>();
		sun.Intensity = 7;
		sun.ShadowSoftness = 2;

		auto& point = entity.AddComponent<PointLightComponent>();
		point.Radius = 3;

		auto& sky = entity.AddComponent<SkyLightComponent>();
		sky.Environment = "Environments/sky.hdr";
		sky.Rotation = 1;
		sky.ShowBackground = false;

		auto& body = entity.AddComponent<RigidBodyComponent>();
		body.Type = RigidBodyType::Kinematic;
		body.Mass = 4;
		body.FixedRotation = true;

		auto& box = entity.AddComponent<BoxColliderComponent>();
		box.HalfExtents = { 1, 2, 3 };
		box.IsTrigger = true;

		auto& sphere = entity.AddComponent<SphereColliderComponent>();
		sphere.Radius = 2;
		sphere.Offset = { 0, 1, 0 };

		auto& script = entity.AddComponent<ScriptComponent>();
		script.Script = "Scripts/Test.lua";
		script.Fields = { { "Speed", 3.5 }, { "Name", "x" }, { "Enabled", true }, { "Offset", { 1, 2, 3 } } };

		auto& audio = entity.AddComponent<AudioSourceComponent>();
		audio.Clip = "Audio/Eat.wav";
		audio.Volume = 0.5f;
		audio.Loop = true;
		audio.Spatial = true;
		return entity;
	}

}

TEST_SUITE("Scene")
{
	TEST_CASE("Entities are created with ID, Tag and Transform")
	{
		Scene scene("Test");
		Entity entity = scene.CreateEntity("Player");
		CHECK(entity.IsValid());
		CHECK(entity.HasComponent<IDComponent>());
		CHECK(entity.GetName() == "Player");
		CHECK(entity.HasComponent<TransformComponent>());
		CHECK(scene.GetEntityCount() == 1);
		CHECK(scene.GetEntityByUUID(entity.GetUUID()) == entity);
		CHECK(scene.FindEntityByName("Player") == entity);
		CHECK_FALSE(scene.FindEntityByName("Nobody"));
		CHECK(scene.CreateEntity().GetName() == "Entity");
	}

	TEST_CASE("Entity order is stable and destruction removes entities")
	{
		Scene scene;
		Entity a = scene.CreateEntity("A");
		Entity b = scene.CreateEntity("B");
		Entity c = scene.CreateEntity("C");
		REQUIRE(scene.GetEntityOrder().size() == 3);
		CHECK(scene.GetEntityOrder()[1] == b.GetUUID());

		const UUID uuid = b.GetUUID();
		scene.DestroyEntity(b);
		CHECK_FALSE(b.IsValid());
		CHECK_FALSE(scene.HasEntity(uuid));
		CHECK(scene.GetEntityOrder().size() == 2);
		CHECK(scene.GetEntityOrder()[1] == c.GetUUID());

		// Destroying an invalid entity or one from another scene is a no-op.
		scene.DestroyEntity(b);
		Scene other;
		scene.DestroyEntity(other.CreateEntity());
		CHECK(scene.GetEntityCount() == 2);
		CHECK(a.IsValid());
	}

	TEST_CASE("Components can be added, queried and removed")
	{
		Scene scene;
		Entity entity = scene.CreateEntity();
		CHECK_FALSE(entity.HasComponent<MeshRendererComponent>());
		CHECK(entity.TryGetComponent<MeshRendererComponent>() == nullptr);
		entity.AddComponent<MeshRendererComponent>().Mesh = "Primitive:Sphere";
		CHECK(entity.GetComponent<MeshRendererComponent>().Mesh == "Primitive:Sphere");
		entity.AddOrReplaceComponent<MeshRendererComponent>().Mesh = "Primitive:Plane";
		CHECK(entity.GetComponent<MeshRendererComponent>().Mesh == "Primitive:Plane");
		entity.RemoveComponent<MeshRendererComponent>();
		CHECK_FALSE(entity.HasComponent<MeshRendererComponent>());
		CHECK_FALSE(Entity().HasComponent<TransformComponent>());
	}

	TEST_CASE("Transform helpers")
	{
		TransformComponent transform;
		CHECK(transform.GetForward().z == doctest::Approx(-1.0f));
		transform.Rotation = { 0.0f, glm::radians(90.0f), 0.0f };
		CHECK(transform.GetForward().x == doctest::Approx(-1.0f));
		CHECK(transform.GetRight().z == doctest::Approx(-1.0f));
		CHECK(transform.GetUp().y == doctest::Approx(1.0f));
		transform.SetRotationQuat(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
		CHECK(transform.Rotation.y == doctest::Approx(0.0f));
		transform.Translation = { 1, 2, 3 };
		CHECK(transform.GetTransform()[3][2] == doctest::Approx(3.0f));
	}

	TEST_CASE("Camera projections")
	{
		CameraComponent camera;
		const glm::mat4 perspective = camera.GetProjection(2.0f);
		CHECK(perspective[0][0] == doctest::Approx(perspective[1][1] / 2.0f));
		CHECK(camera.GetNear() == doctest::Approx(0.1f));
		camera.Projection = ProjectionType::Orthographic;
		CHECK(camera.GetFar() == doctest::Approx(100.0f));
		const glm::mat4 orthographic = camera.GetProjection(0.0f); // invalid aspect falls back to 1
		CHECK(orthographic[0][0] == doctest::Approx(orthographic[1][1]));
	}

	TEST_CASE("Primary camera selection")
	{
		Scene scene;
		CHECK_FALSE(scene.GetPrimaryCameraEntity());
		Entity secondary = scene.CreateEntity("Secondary");
		secondary.AddComponent<CameraComponent>().Primary = false;
		CHECK(scene.GetPrimaryCameraEntity() == secondary); // falls back to any camera
		Entity primary = scene.CreateEntity("Primary");
		primary.AddComponent<CameraComponent>().Primary = true;
		CHECK(scene.GetPrimaryCameraEntity() == primary);
	}

	TEST_CASE("Duplicate and copy preserve components")
	{
		auto scene = CreateRef<Scene>("Original");
		Entity original = CreateFullEntity(*scene);

		Entity duplicate = scene->DuplicateEntity(original);
		CHECK(duplicate.GetUUID() != original.GetUUID());
		CHECK(duplicate.GetName() == "Everything");
		CHECK(duplicate.GetComponent<ScriptComponent>().Fields["Speed"] == 3.5);
		CHECK(duplicate.GetComponent<RigidBodyComponent>().Type == RigidBodyType::Kinematic);

		Ref<Scene> copy = Scene::Copy(scene);
		CHECK(copy->GetName() == "Original");
		CHECK(copy->GetEntityCount() == 2);
		Entity copied = copy->GetEntityByUUID(original.GetUUID());
		REQUIRE(copied);
		CHECK(copied.GetComponent<MeshRendererComponent>().Mesh == "Meshes/Apple.gltf");

		// The copy is independent of the original.
		copied.GetComponent<MeshRendererComponent>().Mesh = "Changed";
		CHECK(original.GetComponent<MeshRendererComponent>().Mesh == "Meshes/Apple.gltf");
	}

	TEST_CASE("ComponentRegistry covers every component")
	{
		const std::vector<std::string> expected = { "Tag", "Transform", "Camera", "MeshRenderer", "DirectionalLight", "PointLight",
			"SkyLight", "RigidBody", "BoxCollider", "SphereCollider", "Script", "AudioSource" };
		REQUIRE(ComponentRegistry::GetAll().size() == expected.size());
		for (const std::string& name : expected)
			CHECK(ComponentRegistry::Find(name) != nullptr);
		CHECK(ComponentRegistry::Find("Nope") == nullptr);
		CHECK_FALSE(ComponentRegistry::Find("Transform")->Removable);
		CHECK_FALSE(ComponentRegistry::Find("Tag")->Removable);

		Scene scene;
		Entity entity = scene.CreateEntity();
		for (const ComponentInfo& info : ComponentRegistry::GetAll())
		{
			info.Add(entity);
			CHECK(info.Has(entity));
			const json data = info.Serialize(entity);
			CHECK(data.is_object());
			info.Deserialize(entity, data);
			if (info.Removable)
			{
				info.Remove(entity);
				CHECK_FALSE(info.Has(entity));
				info.Remove(entity); // no-op when absent
			}
		}
	}

	TEST_CASE("Component patches only change the given fields and are atomic")
	{
		Scene scene;
		Entity entity = scene.CreateEntity();
		const ComponentInfo& info = *ComponentRegistry::Find("PointLight");
		info.Deserialize(entity, json{ { "Intensity", 5.0 } });
		CHECK(entity.GetComponent<PointLightComponent>().Intensity == doctest::Approx(5.0f));
		CHECK(entity.GetComponent<PointLightComponent>().Radius == doctest::Approx(10.0f));

		CHECK_THROWS_AS(info.Deserialize(entity, json{ { "Radius", 2.0 }, { "Color", "red" } }), json::exception);
		// A failed patch leaves the component untouched.
		CHECK(entity.GetComponent<PointLightComponent>().Radius == doctest::Approx(10.0f));

		CHECK_THROWS_AS(ComponentRegistry::Find("Transform")->Deserialize(entity, json{ { "Translation", { 1, 2 } } }), json::exception);
		CHECK_THROWS_AS(ComponentRegistry::Find("Camera")->Deserialize(entity, json{ { "Projection", "Fisheye" } }), json::exception);
		CHECK_THROWS_AS(ComponentRegistry::Find("RigidBody")->Deserialize(entity, json{ { "Type", "Floating" } }), json::exception);
		CHECK_THROWS_AS(ComponentRegistry::Find("Tag")->Deserialize(entity, json::array()), json::exception);
	}

	TEST_CASE("Script fields are merged and null removes a field")
	{
		ScriptComponent script;
		script.Fields = { { "A", 1 }, { "B", 2 } };
		from_json(json{ { "Fields", { { "B", 3 }, { "C", 4 } } } }, script);
		CHECK(script.Fields["A"] == 1);
		CHECK(script.Fields["B"] == 3);
		CHECK(script.Fields["C"] == 4);
		from_json(json{ { "Fields", { { "A", nullptr } } } }, script);
		CHECK_FALSE(script.Fields.contains("A"));
		CHECK_THROWS_AS(from_json(json{ { "Fields", json::array() } }, script), json::exception);
	}

	TEST_CASE("Enum string conversion")
	{
		CHECK(std::string(ProjectionTypeToString(ProjectionType::Orthographic)) == "Orthographic");
		CHECK(ProjectionTypeFromString("Perspective") == ProjectionType::Perspective);
		CHECK_FALSE(ProjectionTypeFromString("x"));
		CHECK(std::string(RigidBodyTypeToString(RigidBodyType::Dynamic)) == "Dynamic");
		CHECK(RigidBodyTypeFromString("Kinematic") == RigidBodyType::Kinematic);
		CHECK_FALSE(RigidBodyTypeFromString("x"));
	}

	TEST_CASE("Scene serialization round-trips every component")
	{
		auto scene = CreateRef<Scene>("RoundTrip");
		CreateFullEntity(*scene);
		scene->CreateEntity("Second");

		Testing::TemporaryDirectory directory;
		const std::filesystem::path path = directory.Path() / "Scene.oscene";
		REQUIRE(SceneSerializer(scene).SaveToFile(path));

		auto loaded = CreateRef<Scene>();
		std::string error;
		REQUIRE(SceneSerializer(loaded).LoadFromFile(path, &error));
		CHECK(loaded->GetName() == "RoundTrip");
		REQUIRE(loaded->GetEntityCount() == 2);
		CHECK(loaded->GetEntityOrder()[1] == scene->GetEntityOrder()[1]);

		Entity entity = loaded->GetEntityByUUID(12345);
		REQUIRE(entity);
		CHECK(entity.GetTransform().Scale.x == doctest::Approx(2.0f));
		CHECK(entity.GetComponent<CameraComponent>().Projection == ProjectionType::Orthographic);
		CHECK(entity.GetComponent<MeshRendererComponent>().Color.w == doctest::Approx(0.8f));
		CHECK(entity.GetComponent<MeshRendererComponent>().Metallic == doctest::Approx(0.3f));
		CHECK_FALSE(entity.GetComponent<MeshRendererComponent>().CastShadows);
		CHECK(entity.GetComponent<DirectionalLightComponent>().Intensity == doctest::Approx(7.0f));
		CHECK(entity.GetComponent<PointLightComponent>().Radius == doctest::Approx(3.0f));
		CHECK_FALSE(entity.GetComponent<SkyLightComponent>().ShowBackground);
		CHECK(entity.GetComponent<RigidBodyComponent>().FixedRotation);
		CHECK(entity.GetComponent<BoxColliderComponent>().IsTrigger);
		CHECK(entity.GetComponent<SphereColliderComponent>().Offset.y == doctest::Approx(1.0f));
		CHECK(entity.GetComponent<ScriptComponent>().Fields["Offset"] == json({ 1, 2, 3 }));
		CHECK(entity.GetComponent<AudioSourceComponent>().Spatial);

		// Serializing the loaded scene gives the same document.
		CHECK(SceneSerializer(loaded).Serialize() == SceneSerializer(scene).Serialize());
	}

	TEST_CASE("Scene deserialization reports errors and leaves the scene empty")
	{
		auto scene = CreateRef<Scene>();
		scene->CreateEntity("Existing");
		SceneSerializer serializer(scene);
		std::string error;

		CHECK_FALSE(serializer.Deserialize(json{ { "Ouroboros", "Project" } }, &error));
		CHECK(error.find("not an Ouroboros scene") != std::string::npos);
		CHECK(scene->GetEntityCount() == 0);

		CHECK_FALSE(serializer.Deserialize(json{ { "Ouroboros", "Scene" }, { "Version", 99 } }, &error));
		CHECK(error.find("version") != std::string::npos);

		const json badComponent = {
			{ "Ouroboros", "Scene" }, { "Version", 1 },
			{ "Entities", { { { "UUID", 1 }, { "Components", { { "Tag", { { "Tag", "A" } } } } } },
							{ { "UUID", 2 }, { "Components", { { "PointLight", { { "Radius", "far" } } } } } } } }
		};
		CHECK_FALSE(serializer.Deserialize(badComponent, &error));
		CHECK(scene->GetEntityCount() == 0);

		Testing::TemporaryDirectory directory;
		CHECK_FALSE(serializer.LoadFromFile(directory.Path() / "missing.oscene", &error));
		FileSystem::WriteTextFile(directory.Path() / "broken.oscene", "{ not json");
		CHECK_FALSE(serializer.LoadFromFile(directory.Path() / "broken.oscene", &error));
		CHECK(error.find("invalid JSON") != std::string::npos);
	}

	TEST_CASE("Unknown components are skipped and clashing UUIDs are replaced")
	{
		Scene scene;
		Entity existing = scene.CreateEntityWithUUID(7, "Existing");
		const json data = { { "UUID", 7 }, { "Components", { { "Tag", { { "Tag", "Clash" } } }, { "FutureComponent", json::object() } } } };
		Entity created = SceneSerializer::DeserializeEntity(scene, data);
		CHECK(created.GetName() == "Clash");
		CHECK(created.GetUUID() != existing.GetUUID());
		CHECK(scene.GetEntityCount() == 2);

		Entity fresh = SceneSerializer::DeserializeEntity(scene, json{ { "UUID", 99 } }, false);
		CHECK(fresh.GetUUID() != 99);
		CHECK_THROWS(SceneSerializer::DeserializeEntity(scene, json::array()));
	}

	TEST_CASE("The shipped scenes load")
	{
		Testing::ScopedProject project;
		for (const char* path : { "Scenes/Snake.oscene", "Scenes/FeatureTest.oscene" })
		{
			auto scene = CreateRef<Scene>();
			std::string error;
			CHECK_MESSAGE(SceneSerializer(scene).LoadFromFile(Project::ResolveAssetPath(path), &error), error);
			CHECK(scene->GetEntityCount() > 0);
			CHECK(scene->GetPrimaryCameraEntity());
		}
	}

	TEST_CASE("Runtime start, update and stop without systems in use")
	{
		Scene scene("Runtime");
		scene.CreateEntity("Empty");
		CHECK_FALSE(scene.IsRunning());
		scene.OnUpdateRuntime(0.016f); // ignored while not running

		scene.OnRuntimeStart();
		CHECK(scene.IsRunning());
		CHECK(scene.GetPhysicsWorld() != nullptr);
		CHECK(scene.GetScriptRuntime() != nullptr);
		CHECK(scene.GetAudio() != nullptr);
		scene.OnRuntimeStart(); // idempotent

		scene.OnUpdateRuntime(0.5f);
		CHECK(scene.GetTime() == doctest::Approx(Scene::FixedTimestep * Scene::MaxFixedStepsPerFrame)); // large steps are clamped
		CHECK(scene.GetFrameIndex() == 1);

		scene.SetPaused(true);
		scene.OnUpdateRuntime(0.016f);
		CHECK(scene.GetFrameIndex() == 1);
		scene.Step(2);
		scene.OnUpdateRuntime(0.016f);
		CHECK(scene.GetFrameIndex() == 2);
		scene.SetPaused(false);

		scene.SetTimeScale(-1.0f);
		CHECK(scene.GetTimeScale() == doctest::Approx(0.0f));
		scene.SetTimeScale(1.0f);

		scene.RequestSceneLoad("Scenes/Other.oscene");
		CHECK(scene.ConsumePendingSceneLoad() == "Scenes/Other.oscene");
		CHECK(scene.ConsumePendingSceneLoad().empty());
		scene.RequestQuit();
		CHECK(scene.ConsumeQuitRequest());
		CHECK_FALSE(scene.ConsumeQuitRequest());

		scene.OnRuntimeStop();
		CHECK_FALSE(scene.IsRunning());
		CHECK(scene.GetPhysicsWorld() == nullptr);
	}
}
