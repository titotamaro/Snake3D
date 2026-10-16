#pragma once

#include <glm/glm.hpp>

#include <filesystem>

struct ma_engine;

namespace Ouroboros {

	// Process-wide miniaudio engine.
	class AudioEngine
	{
	public:
		// Initializes audio output. With `noDevice` (or when no output device is available) the engine runs silently,
		// which keeps headless runs and automated tests deterministic.
		static bool Init(bool noDevice = false);
		static void Shutdown();

		static bool IsInitialized();
		static bool HasDevice();
		static ma_engine* GetEngine();

		// Fire-and-forget playback of an audio file (absolute path).
		static bool PlayOneShot(const std::filesystem::path& path, float volume = 1.0f);
		// Releases finished one-shot sounds. Called once per frame by the application.
		static void Update();
		static size_t GetActiveOneShotCount();

		static void SetMasterVolume(float volume);
		static float GetMasterVolume();

		static void SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);
	};

}
