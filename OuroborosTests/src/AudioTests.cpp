#include "TestUtilities.h"

#include <Ouroboros/Audio/AudioEngine.h>
#include <Ouroboros/Audio/SceneAudio.h>
#include <Ouroboros/Core/FileSystem.h>
#include <Ouroboros/Scene/Entity.h>
#include <Ouroboros/Scene/Scene.h>

using namespace Ouroboros;

TEST_SUITE("Audio")
{
	TEST_CASE("The audio engine runs without an output device")
	{
		AudioEngine::Shutdown();
		REQUIRE(AudioEngine::Init(true));
		CHECK(AudioEngine::IsInitialized());
		CHECK_FALSE(AudioEngine::HasDevice());
		CHECK(AudioEngine::GetEngine() != nullptr);
		CHECK(AudioEngine::Init(true)); // already initialized

		AudioEngine::SetMasterVolume(0.25f);
		CHECK(AudioEngine::GetMasterVolume() == doctest::Approx(0.25f));
		AudioEngine::SetMasterVolume(1.0f);
		AudioEngine::SetListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	TEST_CASE("One-shot sounds validate their files")
	{
		Testing::ScopedProject project;
		REQUIRE(AudioEngine::Init(true));

		CHECK(AudioEngine::PlayOneShot(Project::ResolveAssetPath("Audio/Eat.wav"), 0.5f));
		AudioEngine::Update();
		CHECK(AudioEngine::GetActiveOneShotCount() == 0); // nothing is kept alive without a device

		Testing::LogErrorCounter errors;
		CHECK_FALSE(AudioEngine::PlayOneShot(Project::ResolveAssetPath("Audio/Missing.wav")));
		CHECK(errors.GetCount() == 1);

		Testing::TemporaryDirectory directory;
		const std::filesystem::path garbage = directory.Path() / "NotAudio.wav";
		FileSystem::WriteTextFile(garbage, "this is not a wav file");
		CHECK_FALSE(AudioEngine::PlayOneShot(garbage));
	}

	TEST_CASE("SceneAudio plays AudioSource components")
	{
		Testing::ScopedProject project;
		REQUIRE(AudioEngine::Init(true));

		Scene scene("Audio");
		Entity music = scene.CreateEntity("Music");
		auto& source = music.AddComponent<AudioSourceComponent>();
		source.Clip = "Audio/Music.wav";
		source.Loop = true;
		source.PlayOnStart = true;
		Entity silent = scene.CreateEntity("Silent");
		silent.AddComponent<AudioSourceComponent>().Clip = "Audio/Eat.wav";
		Entity empty = scene.CreateEntity("Empty");
		empty.AddComponent<AudioSourceComponent>();

		SceneAudio audio(&scene);
		audio.Start();
		CHECK(audio.GetActiveSoundCount() == 1);
		CHECK(audio.IsPlaying(music));
		CHECK_FALSE(audio.IsPlaying(silent));

		CHECK(audio.Play(silent));
		CHECK(audio.IsPlaying(silent));
		audio.StopSource(silent);
		CHECK_FALSE(audio.IsPlaying(silent));
		CHECK_FALSE(audio.Play(empty)); // no clip

		// Parameter changes are applied by Update; changing the clip or removing the component releases the sound.
		source.Volume = 0.1f;
		source.Spatial = true;
		audio.Update();
		music.GetComponent<AudioSourceComponent>().Clip = "Audio/Turn.wav";
		audio.Update();
		CHECK_FALSE(audio.IsPlaying(music));
		CHECK(audio.GetActiveSoundCount() == 1);
		silent.RemoveComponent<AudioSourceComponent>();
		audio.Update();
		CHECK(audio.GetActiveSoundCount() == 0);

		Testing::LogErrorCounter errors;
		Entity broken = scene.CreateEntity("Broken");
		broken.AddComponent<AudioSourceComponent>().Clip = "Audio/Missing.wav";
		CHECK_FALSE(audio.Play(broken));
		CHECK(errors.GetCount() == 1);

		audio.OnEntityDestroyed(broken);
		audio.Stop();
		CHECK(audio.GetActiveSoundCount() == 0);
	}
}
