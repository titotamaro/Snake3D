#include "obpch.h"
#include "Ouroboros/Audio/AudioEngine.h"

#include <miniaudio.h>

namespace Ouroboros {

	namespace {

		struct AudioEngineData
		{
			ma_engine Engine{};
			bool Initialized = false;
			bool HasDevice = false;
			float MasterVolume = 1.0f;
			std::vector<Scope<ma_sound>> OneShots;
		};

		AudioEngineData s_Data;

		void CollectFinishedOneShots()
		{
			auto& oneShots = s_Data.OneShots;
			for (auto it = oneShots.begin(); it != oneShots.end();)
			{
				if (ma_sound_at_end(it->get()) || !ma_sound_is_playing(it->get()))
				{
					ma_sound_uninit(it->get());
					it = oneShots.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

	}

	bool AudioEngine::Init(bool noDevice)
	{
		if (s_Data.Initialized)
			return true;

		if (!noDevice)
		{
			ma_engine_config config = ma_engine_config_init();
			if (ma_engine_init(&config, &s_Data.Engine) == MA_SUCCESS)
			{
				s_Data.Initialized = true;
				s_Data.HasDevice = true;
				OB_CORE_INFO("Audio: initialized ({} Hz, {} channels)", ma_engine_get_sample_rate(&s_Data.Engine), ma_engine_get_channels(&s_Data.Engine));
				return true;
			}
			OB_CORE_WARN("Audio: no output device available, continuing without sound");
		}

		ma_engine_config config = ma_engine_config_init();
		config.noDevice = MA_TRUE;
		config.channels = 2;
		config.sampleRate = 48000;
		if (ma_engine_init(&config, &s_Data.Engine) != MA_SUCCESS)
		{
			OB_CORE_ERROR("Audio: failed to initialize the audio engine");
			return false;
		}

		s_Data.Initialized = true;
		s_Data.HasDevice = false;
		OB_CORE_INFO("Audio: initialized without an output device");
		return true;
	}

	void AudioEngine::Shutdown()
	{
		if (!s_Data.Initialized)
			return;

		for (Scope<ma_sound>& sound : s_Data.OneShots)
			ma_sound_uninit(sound.get());
		s_Data.OneShots.clear();

		ma_engine_uninit(&s_Data.Engine);
		s_Data.Initialized = false;
		s_Data.HasDevice = false;
		s_Data.MasterVolume = 1.0f;
	}

	bool AudioEngine::IsInitialized()
	{
		return s_Data.Initialized;
	}

	bool AudioEngine::HasDevice()
	{
		return s_Data.HasDevice;
	}

	ma_engine* AudioEngine::GetEngine()
	{
		return s_Data.Initialized ? &s_Data.Engine : nullptr;
	}

	bool AudioEngine::PlayOneShot(const std::filesystem::path& path, float volume)
	{
		if (!s_Data.Initialized)
			return false;

		if (!std::filesystem::exists(path))
		{
			OB_CORE_ERROR("Audio: file not found '{}'", path.string());
			return false;
		}

		CollectFinishedOneShots();

		// Each one-shot owns its ma_sound; finished sounds are released by Update() or Shutdown().
		Scope<ma_sound> sound = CreateScope<ma_sound>();
		const ma_uint32 flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION;
		if (ma_sound_init_from_file(&s_Data.Engine, path.string().c_str(), flags, nullptr, nullptr, sound.get()) != MA_SUCCESS)
		{
			OB_CORE_ERROR("Audio: could not load '{}'", path.string());
			return false;
		}

		// Without an output device nothing is mixed, so the sound would never finish; it has been validated, release it.
		if (!s_Data.HasDevice)
		{
			ma_sound_uninit(sound.get());
			return true;
		}

		ma_sound_set_volume(sound.get(), volume);
		if (ma_sound_start(sound.get()) != MA_SUCCESS)
		{
			ma_sound_uninit(sound.get());
			return false;
		}

		s_Data.OneShots.push_back(std::move(sound));
		return true;
	}

	void AudioEngine::Update()
	{
		CollectFinishedOneShots();
	}

	size_t AudioEngine::GetActiveOneShotCount()
	{
		return s_Data.OneShots.size();
	}

	void AudioEngine::SetMasterVolume(float volume)
	{
		s_Data.MasterVolume = volume;
		if (s_Data.Initialized)
			ma_engine_set_volume(&s_Data.Engine, volume);
	}

	float AudioEngine::GetMasterVolume()
	{
		return s_Data.MasterVolume;
	}

	void AudioEngine::SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up)
	{
		if (!s_Data.Initialized)
			return;
		ma_engine_listener_set_position(&s_Data.Engine, 0, position.x, position.y, position.z);
		ma_engine_listener_set_direction(&s_Data.Engine, 0, forward.x, forward.y, forward.z);
		ma_engine_listener_set_world_up(&s_Data.Engine, 0, up.x, up.y, up.z);
	}

}
