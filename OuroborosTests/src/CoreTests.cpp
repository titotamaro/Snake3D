#include "TestUtilities.h"

#include <Ouroboros/Core/Event.h>
#include <Ouroboros/Core/FileSystem.h>
#include <Ouroboros/Core/Input.h>
#include <Ouroboros/Core/Log.h>
#include <Ouroboros/Core/Timestep.h>
#include <Ouroboros/Core/UUID.h>

#include <unordered_set>

using namespace Ouroboros;

TEST_SUITE("Core")
{
	TEST_CASE("UUIDs are non-zero, unique and fit in 53 bits")
	{
		std::unordered_set<uint64_t> seen;
		for (int i = 0; i < 20000; i++)
		{
			const uint64_t value = UUID();
			CHECK(value != 0);
			CHECK(value <= UUID::MaxValue);
			seen.insert(value);
		}
		CHECK(seen.size() == 20000);

		const UUID fixed(42);
		CHECK(static_cast<uint64_t>(fixed) == 42);
		CHECK(std::hash<UUID>()(fixed) == std::hash<uint64_t>()(42));
	}

	TEST_CASE("Timestep converts units and Timer measures elapsed time")
	{
		const Timestep timestep(0.25f);
		CHECK(timestep.GetSeconds() == doctest::Approx(0.25f));
		CHECK(timestep.GetMilliseconds() == doctest::Approx(250.0f));
		CHECK(static_cast<float>(timestep) == doctest::Approx(0.25f));

		Timer timer;
		CHECK(timer.Elapsed() >= 0.0f);
		CHECK(timer.ElapsedMillis() >= 0.0f);
		timer.Reset();
		CHECK(timer.Elapsed() < 1.0f);
	}

	TEST_CASE("FileSystem reads and writes text and binary files")
	{
		Testing::TemporaryDirectory directory;
		const std::filesystem::path textPath = directory.Path() / "nested" / "file.txt";
		REQUIRE(FileSystem::WriteTextFile(textPath, "hello\nworld"));
		const auto text = FileSystem::ReadTextFile(textPath);
		REQUIRE(text);
		CHECK(*text == "hello\nworld");

		const uint8_t bytes[] = { 0, 1, 2, 255 };
		const std::filesystem::path binaryPath = directory.Path() / "data.bin";
		REQUIRE(FileSystem::WriteBinaryFile(binaryPath, bytes, sizeof(bytes)));
		const auto binary = FileSystem::ReadBinaryFile(binaryPath);
		REQUIRE(binary);
		CHECK(binary->size() == 4);
		CHECK((*binary)[3] == 255);

		CHECK_FALSE(FileSystem::ReadTextFile(directory.Path() / "missing.txt"));
		CHECK_FALSE(FileSystem::ReadBinaryFile(directory.Path() / "missing.bin"));

		REQUIRE(FileSystem::WriteBinaryFile(directory.Path() / "empty.bin", nullptr, 0));
		const auto empty = FileSystem::ReadBinaryFile(directory.Path() / "empty.bin");
		REQUIRE(empty);
		CHECK(empty->empty());
	}

	TEST_CASE("FileSystem paths")
	{
		CHECK(std::filesystem::exists(FileSystem::GetExecutablePath()));
		CHECK(std::filesystem::is_directory(FileSystem::GetExecutableDirectory()));
		CHECK(std::filesystem::is_directory(FileSystem::GetEngineResourcesDirectory()));

		Testing::TemporaryDirectory directory;
		CHECK(FileSystem::MakeRelativeGeneric(directory.Path() / "a" / "b.txt", directory.Path()) == "a/b.txt");
		const std::filesystem::path outside = directory.Path().parent_path() / "elsewhere.txt";
		CHECK(FileSystem::MakeRelativeGeneric(outside, directory.Path()) == outside.generic_string());

		FileSystem::SetEngineResourcesDirectory(directory.Path());
		CHECK(FileSystem::GetEngineResourcesDirectory() == directory.Path());
		FileSystem::SetEngineResourcesDirectory({});
	}

	TEST_CASE("Input tracks key state with per-frame edges")
	{
		Input::Reset();
		Input::EndFrame();
		CHECK_FALSE(Input::IsKeyDown(Key::W));

		Input::OnKey(Key::W, true);
		CHECK(Input::IsKeyDown(Key::W));
		CHECK(Input::IsKeyPressed(Key::W));
		CHECK_FALSE(Input::IsKeyReleased(Key::W));

		Input::EndFrame();
		CHECK(Input::IsKeyDown(Key::W));
		CHECK_FALSE(Input::IsKeyPressed(Key::W));

		Input::OnKey(Key::W, false);
		CHECK_FALSE(Input::IsKeyDown(Key::W));
		CHECK(Input::IsKeyReleased(Key::W));

		// A press and release within one frame is still reported as pressed.
		Input::EndFrame();
		Input::OnKey(Key::Space, true);
		Input::OnKey(Key::Space, false);
		CHECK(Input::IsKeyPressed(Key::Space));
		CHECK(Input::IsKeyReleased(Key::Space));
		CHECK_FALSE(Input::IsKeyDown(Key::Space));

		// Invalid codes are ignored.
		Input::OnKey(60000, true);
		CHECK_FALSE(Input::IsKeyDown(60000));

		Input::OnMouseButton(Mouse::ButtonLeft, true);
		CHECK(Input::IsMouseButtonDown(Mouse::ButtonLeft));
		CHECK(Input::IsMouseButtonPressed(Mouse::ButtonLeft));
		Input::OnMouseButton(Mouse::ButtonLeft, false);
		CHECK(Input::IsMouseButtonReleased(Mouse::ButtonLeft));

		Input::OnMouseMove(10.0f, 20.0f);
		Input::OnMouseMove(15.0f, 18.0f);
		CHECK(Input::GetMousePosition().x == doctest::Approx(15.0f));
		CHECK(Input::GetMouseDelta().x == doctest::Approx(5.0f));
		Input::OnScroll(0.0f, 2.0f);
		CHECK(Input::GetScrollDelta().y == doctest::Approx(2.0f));

		Input::EndFrame();
		CHECK(Input::GetMouseDelta().x == doctest::Approx(0.0f));
		CHECK(Input::GetScrollDelta().y == doctest::Approx(0.0f));

		Input::OnKey(Key::A, true);
		Input::Reset();
		CHECK_FALSE(Input::IsKeyDown(Key::A));
		CHECK(Input::GetMousePosition().x == doctest::Approx(15.0f)); // mouse position survives a reset
	}

	TEST_CASE("Input key names")
	{
		CHECK(Input::KeyFromName("W") == Key::W);
		CHECK(Input::KeyFromName("space") == Key::Space);
		CHECK(Input::KeyFromName("Up") == Key::Up);
		CHECK(Input::KeyFromName("1") == Key::D1);
		CHECK(Input::KeyFromName("NotAKey") == 0);
		CHECK(std::string(Input::KeyToName(Key::Escape)) == "Escape");
		CHECK(std::string(Input::KeyToName(1)) == "Unknown");
	}

	TEST_CASE("LogBuffer keeps recent messages and counts errors")
	{
		LogBuffer buffer(3);
		buffer.Push(LogLevel::Info, "A", "one");
		buffer.Push(LogLevel::Error, "A", "two");
		buffer.Push(LogLevel::Warn, "A", "three");
		buffer.Push(LogLevel::Critical, "B", "four");

		const auto messages = buffer.GetMessages();
		REQUIRE(messages.size() == 3);
		CHECK(messages.front().Text == "two");
		CHECK(messages.back().Text == "four");
		CHECK(buffer.GetErrorCount() == 2);
		CHECK(buffer.GetNextIndex() == 5);

		const auto newer = buffer.GetMessages(4);
		REQUIRE(newer.size() == 1);
		CHECK(newer.front().Logger == "B");

		buffer.Clear();
		CHECK(buffer.GetMessages().empty());
		CHECK(buffer.GetErrorCount() == 0);
		CHECK(std::string(Log::LevelToString(LogLevel::Warn)) == "warn");
	}

	TEST_CASE("Engine log output reaches the shared buffer")
	{
		Testing::LogErrorCounter errors;
		const uint64_t before = Log::GetBuffer().GetNextIndex();
		OB_CORE_WARN("core warning for test");
		OB_ERROR("client error for test");
		const auto messages = Log::GetBuffer().GetMessages(before);
		REQUIRE(messages.size() == 2);
		CHECK(messages[0].Level == LogLevel::Warn);
		CHECK(messages[1].Logger == "APP");
		CHECK(errors.GetCount() == 1);
	}

	TEST_CASE("EventDispatcher dispatches by type and records handling")
	{
		KeyPressedEvent event(Key::A, false);
		EventDispatcher dispatcher(event);
		bool mouseHandled = false;
		CHECK_FALSE(dispatcher.Dispatch<MouseMovedEvent>([&](MouseMovedEvent&) { mouseHandled = true; return true; }));
		CHECK_FALSE(mouseHandled);
		CHECK(dispatcher.Dispatch<KeyPressedEvent>([](KeyPressedEvent& keyEvent) { return keyEvent.GetKeyCode() == Key::A; }));
		CHECK(event.Handled);
		CHECK(std::string(event.GetName()) == "KeyPressed");

		WindowResizeEvent resize(800, 600);
		CHECK(resize.GetWidth() == 800);
		CHECK(resize.GetEventType() == EventType::WindowResize);
		WindowDropEvent drop({ "a.gltf", "b.wav" });
		CHECK(drop.GetPaths().size() == 2);
	}
}
