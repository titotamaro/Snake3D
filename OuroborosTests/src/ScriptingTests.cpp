#include "TestUtilities.h"

#include <Ouroboros/Audio/AudioEngine.h>
#include <Ouroboros/Core/FileSystem.h>
#include <Ouroboros/Core/Input.h>
#include <Ouroboros/Scene/Entity.h>
#include <Ouroboros/Scene/Scene.h>
#include <Ouroboros/Scripting/ScriptGlue.h>
#include <Ouroboros/Scripting/ScriptRuntime.h>

using namespace Ouroboros;
using json = nlohmann::json;

namespace {

	// A running scene with the Snake project active, for executing Lua.
	struct ScriptFixture
	{
		Testing::ScopedProject Project;
		Ref<Scene> SceneContext = CreateRef<Scene>("ScriptTest");

		ScriptFixture()
		{
			AudioEngine::Init(true);
			SceneContext->SetViewportSize(800, 600);
		}

		~ScriptFixture()
		{
			SceneContext->OnRuntimeStop();
		}

		void Start() { SceneContext->OnRuntimeStart(); }
		ScriptRuntime& Runtime() { return *SceneContext->GetScriptRuntime(); }

		ScriptExecutionResult Run(const std::string& code)
		{
			if (!SceneContext->IsRunning())
				Start();
			return Runtime().Execute(code);
		}

		// Runs `code`, which must return true.
		void Expect(const std::string& code)
		{
			const ScriptExecutionResult result = Run(code);
			CHECK_MESSAGE(result.Success, code, " -> ", result.Error);
			CHECK_MESSAGE(result.Value == "true", code, " returned ", result.Value);
		}
	};

}

TEST_SUITE("Scripting")
{
	TEST_CASE("Execute returns values and reports errors")
	{
		ScriptFixture fixture;
		ScriptExecutionResult result = fixture.Run("return 1 + 2");
		CHECK(result.Success);
		CHECK(result.Value == "3");

		result = fixture.Run("this is not lua");
		CHECK_FALSE(result.Success);
		CHECK(result.Error.find("syntax") != std::string::npos);

		result = fixture.Run("error('boom')");
		CHECK_FALSE(result.Success);
		CHECK(result.Error.find("boom") != std::string::npos);

		result = fixture.Run("local x = 1");
		CHECK(result.Success);
		CHECK(result.Value.empty());
	}

	TEST_CASE("The Lua state is sandboxed")
	{
		ScriptFixture fixture;
		fixture.Expect("return io == nil and os == nil and debug == nil");
		fixture.Expect("return package.cpath == ''");
		fixture.Expect("return package.loadlib == nil and dofile == nil and loadfile == nil");
		fixture.Expect("return not pcall(require, 'definitely_not_a_module')");
		fixture.Expect("return string.format('%d', 5) == '5' and math.floor(1.5) == 1 and table.concat({1,2}) == '12'");
	}

	TEST_CASE("Math types")
	{
		ScriptFixture fixture;
		fixture.Expect("return vec3(1,2,3) + vec3(1) == vec3(2,3,4)");
		fixture.Expect("return (vec3(1,0,0):Cross(vec3(0,1,0))) == vec3(0,0,1)");
		fixture.Expect("return math.abs(vec2(3,4):Length() - 5) < 1e-6");
		fixture.Expect("return vec4(1,2,3,4).w == 4");
		fixture.Expect("local q = quat.AngleAxis(math.pi, vec3(0,1,0)); local v = q:Rotate(vec3(1,0,0)); return math.abs(v.x + 1) < 1e-5");
		fixture.Expect("return Math.Clamp(3, 0, 1) == 1 and Math.Sign(-2) == -1");
	}

	TEST_CASE("Entities and components through the API")
	{
		ScriptFixture fixture;
		fixture.Start();
		fixture.Expect(R"(
			local e = Scene.CreateEntity("Lua Entity")
			e:AddComponent("MeshRenderer", { Mesh = "Primitive:Sphere" })
			e.Transform.Translation = vec3(1, 2, 3)
			return e.MeshRenderer.Mesh == "Primitive:Sphere" and e.Transform.Translation == vec3(1, 2, 3)
		)");

		Entity entity = fixture.SceneContext->FindEntityByName("Lua Entity");
		REQUIRE(entity);
		CHECK(entity.GetComponent<MeshRendererComponent>().Mesh == "Primitive:Sphere");
		CHECK(entity.GetTransform().Translation.y == doctest::Approx(2.0f));

		fixture.Expect("return Scene.FindEntityByName('Lua Entity').ID == " + std::to_string(static_cast<uint64_t>(entity.GetUUID())));
		fixture.Expect("return Scene.FindEntityByName('Lua Entity').PointLight == nil");
		fixture.Expect("return not pcall(function() Scene.FindEntityByName('Lua Entity'):AddComponent('NoSuchComponent') end)");
		fixture.Expect("local e = Scene.FindEntityByName('Lua Entity'); e:Destroy(); return not e:IsValid()");
		CHECK_FALSE(fixture.SceneContext->FindEntityByName("Lua Entity"));
	}

	TEST_CASE("JSON and Lua values convert both ways")
	{
		ScriptFixture fixture;
		fixture.Start();
		sol::state& lua = fixture.Runtime().GetState();

		const json data = { { "Number", 1.5 }, { "Integer", 7 }, { "Flag", true }, { "Text", "hi" }, { "Vector", { 1, 2, 3 } },
			{ "Color", { 1, 0, 0, 1 } }, { "List", { "a", "b" } }, { "Nested", { { "Inner", 2 } } }, { "Nothing", nullptr } };
		sol::object value = ScriptGlue::JsonToLua(lua, data);
		lua["converted"] = value;
		fixture.Expect("return converted.Vector == vec3(1,2,3) and converted.Color == vec4(1,0,0,1) and converted.List[2] == 'b'");
		fixture.Expect("return converted.Nested.Inner == 2 and converted.Nothing == nil and converted.Flag == true");

		const json back = ScriptGlue::LuaToJson(value);
		CHECK(back["Number"] == 1.5);
		CHECK(back["Integer"] == 7);
		CHECK(back["Vector"] == json({ 1, 2, 3 }));
		CHECK(back["List"] == json({ "a", "b" }));
		CHECK(back["Nested"]["Inner"] == 2);
		CHECK_FALSE(back.contains("Nothing"));

		CHECK(ScriptGlue::LuaToJson(sol::make_object(lua, lua.create_table())) == json::object());
		fixture.Run("cyclic = {}; cyclic.self = cyclic");
		CHECK_THROWS(ScriptGlue::LuaToJson(lua["cyclic"]));
		CHECK_THROWS(ScriptGlue::LuaToJson(lua["print"]));
	}

	TEST_CASE("Script components get instances with properties, fields and callbacks")
	{
		ScriptFixture fixture;
		Entity entity = fixture.SceneContext->CreateEntity("Probe");
		auto& script = entity.AddComponent<ScriptComponent>();
		script.Script = "Scripts/Tests/DestroyProbe.lua";
		script.Fields = { { "Name", "fromFields" }, { "Value", 4 } };

		fixture.Start();
		REQUIRE(fixture.Runtime().HasInstance(entity.GetUUID()));
		fixture.Expect("return FeatureTestLifecycle[#FeatureTestLifecycle] == 'create:fromFields'");
		fixture.Expect("local probe = Scene.FindEntityByName('Probe'):GetScript(); return probe:Double() == 8 and probe.Offset == vec3(1,2,3)");

		// Removing the component releases the instance (OnDestroy) on the next update.
		entity.RemoveComponent<ScriptComponent>();
		fixture.SceneContext->OnUpdateRuntime(Scene::FixedTimestep);
		CHECK_FALSE(fixture.Runtime().HasInstance(entity.GetUUID()));
		fixture.Expect("return FeatureTestLifecycle[#FeatureTestLifecycle] == 'destroy:fromFields'");

		// Adding a component while running instantiates it on the next update.
		entity.AddComponent<ScriptComponent>().Script = "Scripts/Tests/DestroyProbe.lua";
		fixture.SceneContext->OnUpdateRuntime(Scene::FixedTimestep);
		CHECK(fixture.Runtime().HasInstance(entity.GetUUID()));
		CHECK(fixture.Runtime().GetInstanceCount() == 1);
	}

	TEST_CASE("Broken scripts are reported once and do not stop the scene")
	{
		ScriptFixture fixture;
		Testing::TemporaryDirectory directory;
		const std::filesystem::path syntaxError = directory.Path() / "Syntax.lua";
		const std::filesystem::path notATable = directory.Path() / "NotATable.lua";
		const std::filesystem::path runtimeError = directory.Path() / "Runtime.lua";
		FileSystem::WriteTextFile(syntaxError, "return {");
		FileSystem::WriteTextFile(notATable, "return 5");
		FileSystem::WriteTextFile(runtimeError, "local S = {} function S:OnUpdate(dt) error('update failed') end return S");

		for (const auto& path : { syntaxError, notATable, runtimeError })
			fixture.SceneContext->CreateEntity(path.stem().string()).AddComponent<ScriptComponent>().Script = path.string();
		fixture.SceneContext->CreateEntity("Missing").AddComponent<ScriptComponent>().Script = "Scripts/DoesNotExist.lua";

		fixture.Start();
		for (int i = 0; i < 5; i++)
			fixture.SceneContext->OnUpdateRuntime(Scene::FixedTimestep);

		const ScriptRuntime& runtime = fixture.Runtime();
		// Three load failures + one OnUpdate failure (the failing callback is disabled after the first error).
		CHECK(runtime.GetErrorCount() == 4);
		CHECK(runtime.GetInstanceCount() == 1);
		bool sawSyntax = false, sawTable = false, sawUpdate = false;
		for (const std::string& error : runtime.GetErrors())
		{
			sawSyntax = sawSyntax || error.find("Syntax error") != std::string::npos;
			sawTable = sawTable || error.find("must return a table") != std::string::npos;
			sawUpdate = sawUpdate || error.find("update failed") != std::string::npos;
		}
		CHECK(sawSyntax);
		CHECK(sawTable);
		CHECK(sawUpdate);
	}

	TEST_CASE("Callbacks run in order: OnCreate, OnUpdate, OnFixedUpdate, OnDestroy")
	{
		ScriptFixture fixture;
		Testing::TemporaryDirectory directory;
		const std::filesystem::path path = directory.Path() / "Order.lua";
		FileSystem::WriteTextFile(path, R"(
			CallLog = {}
			local S = {}
			function S:OnCreate() CallLog[#CallLog + 1] = "create" end
			function S:OnUpdate(dt) CallLog[#CallLog + 1] = "update" end
			function S:OnFixedUpdate(dt) CallLog[#CallLog + 1] = "fixed" end
			function S:OnDestroy() CallLog[#CallLog + 1] = "destroy" end
			return S)");
		Entity entity = fixture.SceneContext->CreateEntity("Ordered");
		entity.AddComponent<ScriptComponent>().Script = path.string();

		fixture.Start();
		fixture.SceneContext->OnUpdateRuntime(Scene::FixedTimestep);
		fixture.Expect("return CallLog[1] == 'create' and CallLog[2] == 'update' and CallLog[3] == 'fixed'");
		fixture.SceneContext->DestroyEntity(entity);
		fixture.Expect("return CallLog[#CallLog] == 'destroy'");
	}

	TEST_CASE("Scene, time, quit and UI requests from scripts")
	{
		ScriptFixture fixture;
		fixture.Run("Scene.Load('Scenes/Snake.oscene'); Application.Quit(); UI.Text('Hello', 1, 2, { Size = 12, Anchor = 'BottomRight' })");
		CHECK(fixture.SceneContext->ConsumePendingSceneLoad() == "Scenes/Snake.oscene");
		CHECK(fixture.SceneContext->ConsumeQuitRequest());
		const auto& ui = fixture.Runtime().GetUIText();
		REQUIRE(ui.size() == 1);
		CHECK(ui[0].Text == "Hello");
		CHECK(ui[0].Anchor == UIAnchor::BottomRight);
		CHECK(ui[0].Size == doctest::Approx(12.0f));

		fixture.Expect("local size = Application.GetViewportSize(); return size.x == 800 and size.y == 600");
		fixture.Expect("Time.SetTimeScale(2); return Time.GetTimeScale() == 2");
		CHECK(fixture.SceneContext->GetTimeScale() == doctest::Approx(2.0f));
	}

	TEST_CASE("Input from scripts")
	{
		ScriptFixture fixture;
		Input::Reset();
		Input::OnKey(Key::Up, true);
		fixture.Expect("return Input.IsKeyDown(Key.Up) and Input.IsKeyPressed('Up') and not Input.IsKeyDown(Key.Down)");
		Input::Reset();
	}

	TEST_CASE("Snake game rules (SnakeLogic.lua)")
	{
		ScriptFixture fixture;
		const ScriptExecutionResult result = fixture.Run(R"(
			local run = require("Tests.SnakeLogicTests")
			local failures, messages, count = run()
			assert(count >= 9, "expected at least 9 tests, found " .. count)
			if failures > 0 then error(messages) end
			return count)");
		CHECK_MESSAGE(result.Success, result.Error);
	}

	TEST_CASE("The game scripts load and the game starts")
	{
		ScriptFixture fixture;
		auto scene = fixture.SceneContext;
		Entity head = scene->CreateEntity("Snake Head");
		head.AddComponent<MeshRendererComponent>().Mesh = "Meshes/SnakeHead.gltf";
		Entity apple = scene->CreateEntity("Apple");
		apple.AddComponent<MeshRendererComponent>().Mesh = "Meshes/Apple.gltf";
		apple.AddComponent<ScriptComponent>().Script = "Scripts/Apple.lua";
		Entity game = scene->CreateEntity("Game");
		game.AddComponent<ScriptComponent>().Script = "Scripts/GameManager.lua";

		fixture.Start();
		for (int i = 0; i < 10; i++)
			scene->OnUpdateRuntime(Scene::FixedTimestep);
		CHECK(fixture.Runtime().GetErrorCount() == 0);
		fixture.Expect("return Scene.FindEntityByName('Game'):GetScript().Mode == 'Ready'");
		fixture.Expect("return #Scene.GetEntitiesWithComponent('BoxCollider') == 84"); // the border walls

		// Start moving and play a few seconds: the snake heads right and eventually hits the wall.
		Input::OnKey(Key::Right, true);
		scene->OnUpdateRuntime(Scene::FixedTimestep);
		Input::EndFrame();
		Input::OnKey(Key::Right, false);
		for (int i = 0; i < 60 * 6; i++)
		{
			scene->OnUpdateRuntime(Scene::FixedTimestep);
			Input::EndFrame();
		}
		fixture.Expect("return Scene.FindEntityByName('Game'):GetScript().Mode == 'GameOver'");
		fixture.Expect("return Scene.FindEntityByName('Snake Head'):HasComponent('RigidBody')"); // exploded into physics bodies
		CHECK(fixture.Runtime().GetErrorCount() == 0);
	}
}
