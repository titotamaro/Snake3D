#include "TestUtilities.h"

#include <Ouroboros/Automation/CommandProcessor.h>
#include <Ouroboros/Automation/CommandServer.h>
#include <Ouroboros/Automation/SceneCommands.h>
#include <Ouroboros/Core/Input.h>
#include <Ouroboros/Scene/Entity.h>
#include <Ouroboros/Scene/Scene.h>

#if defined(OB_PLATFORM_WINDOWS)
	#include <WinSock2.h>
	#include <WS2tcpip.h>
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <unistd.h>
#endif

#include <thread>

using namespace Ouroboros;
using json = nlohmann::json;

namespace {

	struct CommandFixture
	{
		Ref<Scene> SceneContext = CreateRef<Scene>("Automation");
		CommandProcessor Processor;

		CommandFixture()
		{
			SceneCommands::Register(Processor, [this]() { return SceneContext; });
		}

		json Run(const std::string& command, const json& args = json::object())
		{
			return Processor.ExecuteImmediate({ { "id", 1 }, { "cmd", command }, { "args", args } });
		}

		json Ok(const std::string& command, const json& args = json::object())
		{
			const json response = Run(command, args);
			INFO(command, " -> ", response.dump());
			REQUIRE(response["ok"] == true);
			return response["result"];
		}

		std::string Error(const std::string& command, const json& args = json::object())
		{
			const json response = Run(command, args);
			INFO(command, " -> ", response.dump());
			REQUIRE(response["ok"] == false);
			return response["error"].get<std::string>();
		}
	};

}

TEST_SUITE("Automation")
{
	TEST_CASE("Processor basics: ping, help, unknown commands and malformed requests")
	{
		CommandFixture fixture;
		CHECK(fixture.Ok("ping")["pong"] == true);

		const json help = fixture.Ok("help");
		CHECK(help.size() > 15);
		CHECK(fixture.Ok("help", { { "command", "entity.create" } })[0]["name"] == "entity.create");
		CHECK(fixture.Error("help", { { "command", "nope" } }).find("unknown command") != std::string::npos);

		CHECK(fixture.Error("does.not.exist").find("unknown command") != std::string::npos);
		CHECK_FALSE(fixture.Processor.ExecuteImmediate(json::array())["ok"]);
		CHECK_FALSE(fixture.Processor.ExecuteImmediate({ { "cmd", 5 } })["ok"]);
		CHECK_FALSE(fixture.Processor.ExecuteImmediate({ { "cmd", "ping" }, { "args", 5 } })["ok"]);
		CHECK(fixture.Processor.ExecuteImmediate({ { "id", "abc" }, { "cmd", "ping" } })["id"] == "abc");

		std::vector<json> responses;
		fixture.Processor.SubmitLine("{ not json", [&](const json& response) { responses.push_back(response); });
		REQUIRE(responses.size() == 1);
		CHECK(responses[0]["error"] == "invalid JSON");
	}

	TEST_CASE("Queued requests run in order and deferred commands block the queue")
	{
		CommandFixture fixture;
		int polls = 0;
		fixture.Processor.Register("test.wait", "", "{}", [&](const json&) -> CommandResult
		{
			return CommandResult::Deferred([&]() -> std::optional<json>
			{
				if (++polls < 2) // first Update dispatches, the next ones poll
					return std::nullopt;
				return json{ { "waited", polls } };
			});
		});
		fixture.Processor.Register("test.fail", "", "{}", [](const json&) -> CommandResult
		{
			return CommandResult::Deferred([]() -> std::optional<json> { throw CommandError("deferred failure"); });
		});

		std::vector<json> responses;
		auto collect = [&](const json& response) { responses.push_back(response); };
		fixture.Processor.Submit({ { "id", 1 }, { "cmd", "test.wait" } }, collect);
		fixture.Processor.Submit({ { "id", 2 }, { "cmd", "ping" } }, collect);
		fixture.Processor.Submit({ { "id", 3 }, { "cmd", "test.fail" } }, collect);

		fixture.Processor.Update();
		CHECK(responses.empty());
		CHECK_FALSE(fixture.Processor.IsIdle());
		fixture.Processor.Update();
		fixture.Processor.Update();
		REQUIRE(responses.size() == 2);
		CHECK(responses[0]["id"] == 1);
		CHECK(responses[0]["result"]["waited"] == 2);
		CHECK(responses[1]["id"] == 2);
		fixture.Processor.Update();
		REQUIRE(responses.size() == 3);
		CHECK(responses[2]["error"] == "deferred failure");
		CHECK(fixture.Processor.IsIdle());

		CHECK_FALSE(fixture.Processor.ExecuteImmediate({ { "cmd", "test.wait" } })["ok"]);
	}

	TEST_CASE("Entity commands")
	{
		CommandFixture fixture;
		const json created = fixture.Ok("entity.create", { { "name", "Crate" }, { "components", { { "MeshRenderer", { { "Mesh", "Primitive:Cube" } } }, { "Transform", { { "Translation", { 1, 2, 3 } } } } } } });
		const uint64_t id = created["id"];
		CHECK(created["components"] == json({ "Tag", "Transform", "MeshRenderer" }));
		Entity entity = fixture.SceneContext->GetEntityByUUID(id);
		REQUIRE(entity);
		CHECK(entity.GetTransform().Translation.z == doctest::Approx(3.0f));

		CHECK(fixture.Ok("entity.list").size() == 1);
		CHECK(fixture.Ok("entity.list", { { "component", "MeshRenderer" } }).size() == 1);
		CHECK(fixture.Ok("entity.list", { { "component", "Camera" } }).empty());
		CHECK(fixture.Error("entity.list", { { "component", "Bogus" } }).find("unknown component") != std::string::npos);

		CHECK(fixture.Ok("entity.get", { { "entity", "Crate" } })["UUID"] == id);
		CHECK(fixture.Ok("entity.get", { { "id", id } })["Components"]["MeshRenderer"]["Mesh"] == "Primitive:Cube");
		CHECK(fixture.Error("entity.get", { { "entity", "Ghost" } }).find("not found") != std::string::npos);
		CHECK(fixture.Error("entity.get").find("missing argument") != std::string::npos);

		CHECK(fixture.Ok("entity.rename", { { "entity", id }, { "newName", "Box" } })["name"] == "Box");
		const json duplicate = fixture.Ok("entity.duplicate", { { "entity", "Box" } });
		CHECK(duplicate["id"] != id);
		CHECK(fixture.Ok("entity.destroy", { { "entity", duplicate["id"] } })["destroyed"] == duplicate["id"]);
		CHECK(fixture.SceneContext->GetEntityCount() == 1);

		// A failing component in entity.create leaves no half-created entity behind.
		CHECK(fixture.Error("entity.create", { { "name", "Bad" }, { "components", { { "PointLight", { { "Radius", "big" } } } } } }).find("invalid argument") != std::string::npos);
		CHECK(fixture.Error("entity.create", { { "components", { { "Unknown", json::object() } } } }).find("unknown component") != std::string::npos);
		CHECK(fixture.SceneContext->GetEntityCount() == 1);
	}

	TEST_CASE("Component commands")
	{
		CommandFixture fixture;
		fixture.Ok("entity.create", { { "name", "Lamp" } });

		const json types = fixture.Ok("component.types");
		CHECK(types.size() == 12);
		CHECK(types["PointLight"]["defaults"]["Radius"] == 10.0);
		CHECK(types["Transform"]["removable"] == false);

		CHECK(fixture.Ok("component.add", { { "entity", "Lamp" }, { "type", "PointLight" }, { "data", { { "Intensity", 3 } } } })["Intensity"] == 3.0);
		CHECK(fixture.Error("component.add", { { "entity", "Lamp" }, { "type", "PointLight" } }).find("already has") != std::string::npos);
		CHECK(fixture.Ok("component.set", { { "entity", "Lamp" }, { "type", "PointLight" }, { "data", { { "Radius", 2 } } } })["Radius"] == 2.0);
		CHECK(fixture.Ok("component.get", { { "entity", "Lamp" }, { "type", "PointLight" } })["Intensity"] == 3.0);
		CHECK(fixture.Error("component.set", { { "entity", "Lamp" }, { "type", "PointLight" }, { "data", { { "Radius", "x" } } } }).find("invalid argument") != std::string::npos);
		CHECK(fixture.Error("component.set", { { "entity", "Lamp" }, { "type", "PointLight" } }).find("missing argument 'data'") != std::string::npos);

		// component.set adds a missing component.
		CHECK(fixture.Ok("component.set", { { "entity", "Lamp" }, { "type", "Camera" }, { "data", { { "Primary", false } } } })["Primary"] == false);

		CHECK(fixture.Ok("component.remove", { { "entity", "Lamp" }, { "type", "PointLight" } })["components"].size() == 3);
		CHECK(fixture.Error("component.remove", { { "entity", "Lamp" }, { "type", "PointLight" } }).find("has no") != std::string::npos);
		CHECK(fixture.Error("component.remove", { { "entity", "Lamp" }, { "type", "Transform" } }).find("cannot be removed") != std::string::npos);
		CHECK(fixture.Error("component.get", { { "entity", "Lamp" }, { "type", "Nope" } }).find("unknown component type") != std::string::npos);
		CHECK(fixture.Error("component.get", { { "entity", "Lamp" }, { "type", "SkyLight" } }).find("has no") != std::string::npos);
	}

	TEST_CASE("Scene, script, input and log commands")
	{
		Testing::ScopedProject project;
		CommandFixture fixture;
		fixture.Ok("entity.create", { { "name", "A" } });

		const json info = fixture.Ok("scene.info");
		CHECK(info["name"] == "Automation");
		CHECK(info["entityCount"] == 1);
		CHECK(info["running"] == false);
		CHECK(fixture.Ok("scene.get")["Entities"].size() == 1);

		CHECK(fixture.Error("script.exec", { { "code", "return 1" } }).find("play mode") != std::string::npos);
		CHECK(fixture.Ok("script.errors")["count"] == 0);
		fixture.SceneContext->OnRuntimeStart();
		CHECK(fixture.Ok("script.exec", { { "code", "return Scene.GetEntityCount()" } })["value"] == "1");
		CHECK(fixture.Error("script.exec", { { "code", "error('nope')" } }).find("nope") != std::string::npos);
		fixture.SceneContext->OnRuntimeStop();

		Input::Reset();
		CHECK(fixture.Ok("input.key", { { "key", "W" }, { "down", true } })["key"] == "W");
		CHECK(Input::IsKeyDown(Key::W));
		fixture.Ok("input.key", { { "key", static_cast<int>(Key::W) }, { "down", false } });
		CHECK_FALSE(Input::IsKeyDown(Key::W));
		CHECK(fixture.Error("input.key", { { "key", "Nope" }, { "down", true } }).find("unknown key") != std::string::npos);

		std::vector<json> responses;
		fixture.Processor.Submit({ { "cmd", "input.tap" }, { "args", { { "key", "Space" }, { "frames", 2 } } } }, [&](const json& response) { responses.push_back(response); });
		fixture.Processor.Update();
		CHECK(Input::IsKeyDown(Key::Space));
		fixture.Processor.Update();
		CHECK(responses.empty());
		fixture.Processor.Update();
		REQUIRE(responses.size() == 1);
		CHECK_FALSE(Input::IsKeyDown(Key::Space));

		OB_CORE_WARN("automation log test");
		const json log = fixture.Ok("log.get", { { "level", "warn" } });
		CHECK(log["messages"].size() >= 1);
		CHECK(log["messages"].back()["text"] == "automation log test");
		const json newer = fixture.Ok("log.get", { { "since", log["next"] } });
		CHECK(newer["messages"].empty());
		CHECK(fixture.Ok("log.get", { { "limit", 1 } })["messages"].size() == 1);
	}

	TEST_CASE("Commands fail cleanly without a scene")
	{
		CommandProcessor processor;
		SceneCommands::Register(processor, []() { return Ref<Scene>(); });
		const json response = processor.ExecuteImmediate({ { "cmd", "entity.list" } });
		CHECK_FALSE(response["ok"]);
		CHECK(response["error"] == "no scene is open");
	}

	TEST_CASE("The TCP command server answers JSON lines")
	{
		CommandProcessor processor;
		CommandServer server(processor);
		REQUIRE(server.Start(0));
		CHECK(server.IsRunning());
		REQUIRE(server.GetPort() != 0);

#if defined(OB_PLATFORM_WINDOWS)
		using Socket = SOCKET;
		auto closeSocket = [](Socket socket) { closesocket(socket); };
#else
		using Socket = int;
		auto closeSocket = [](Socket socket) { close(socket); };
#endif
		Socket client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(server.GetPort());
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		REQUIRE(connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);

		const std::string request = "{\"id\": 7, \"cmd\": \"ping\"}\n{\"id\": 8, \"cmd\": \"nope\"}\n";
		send(client, request.data(), static_cast<int>(request.size()), 0);

		std::string received;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (std::count(received.begin(), received.end(), '\n') < 2 && std::chrono::steady_clock::now() < deadline)
		{
			server.Poll();
			processor.Update();

			fd_set readSet;
			FD_ZERO(&readSet);
			FD_SET(client, &readSet);
			timeval timeout{ 0, 10000 };
			if (select(static_cast<int>(client + 1), &readSet, nullptr, nullptr, &timeout) > 0)
			{
				char buffer[1024];
				const int count = static_cast<int>(recv(client, buffer, sizeof(buffer), 0));
				if (count <= 0)
					break;
				received.append(buffer, static_cast<size_t>(count));
			}
		}
		closeSocket(client);
		server.Stop();
		CHECK_FALSE(server.IsRunning());

		REQUIRE(std::count(received.begin(), received.end(), '\n') == 2);
		const json first = json::parse(received.substr(0, received.find('\n')));
		const json second = json::parse(received.substr(received.find('\n') + 1));
		CHECK(first["id"] == 7);
		CHECK(first["result"]["pong"] == true);
		CHECK(second["id"] == 8);
		CHECK(second["ok"] == false);
	}
}
