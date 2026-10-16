#include "obpch.h"
#include "Ouroboros/Audio/SceneAudio.h"

#include "Ouroboros/Audio/AudioEngine.h"
#include "Ouroboros/Project/Project.h"
#include "Ouroboros/Scene/Entity.h"

#include <miniaudio.h>

namespace Ouroboros {

	struct SceneAudio::SoundInstance
	{
		ma_sound Sound{};
		std::string Clip;
		bool Initialized = false;

		~SoundInstance()
		{
			if (Initialized)
				ma_sound_uninit(&Sound);
		}
	};

	SceneAudio::SceneAudio(Scene* scene)
		: m_Scene(scene)
	{
	}

	SceneAudio::~SceneAudio()
	{
		Stop();
	}

	void SceneAudio::Start()
	{
		auto view = m_Scene->GetRegistry().view<AudioSourceComponent>();
		for (entt::entity handle : view)
		{
			Entity entity(handle, m_Scene);
			if (view.get<AudioSourceComponent>(handle).PlayOnStart)
				Play(entity);
		}
	}

	SceneAudio::SoundInstance* SceneAudio::GetOrCreateSound(Entity entity)
	{
		const AudioSourceComponent* source = entity.TryGetComponent<AudioSourceComponent>();
		ma_engine* engine = AudioEngine::GetEngine();
		if (!source || !engine || source->Clip.empty())
			return nullptr;

		const UUID uuid = entity.GetUUID();
		if (auto it = m_Sounds.find(uuid); it != m_Sounds.end())
		{
			if (it->second->Clip == source->Clip)
				return it->second.get();
			ReleaseSound(uuid); // the clip changed
		}

		const std::filesystem::path path = Project::ResolveAssetPath(source->Clip);
		if (!std::filesystem::exists(path))
		{
			OB_CORE_ERROR("Audio: clip '{}' on entity '{}' not found", source->Clip, entity.GetName());
			return nullptr;
		}

		Scope<SoundInstance> instance = CreateScope<SoundInstance>();
		if (ma_sound_init_from_file(engine, path.string().c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, &instance->Sound) != MA_SUCCESS)
		{
			OB_CORE_ERROR("Audio: could not load clip '{}'", path.string());
			return nullptr;
		}
		instance->Initialized = true;
		instance->Clip = source->Clip;

		SoundInstance* result = instance.get();
		m_Sounds[uuid] = std::move(instance);
		return result;
	}

	void SceneAudio::ReleaseSound(UUID uuid)
	{
		m_Sounds.erase(uuid);
	}

	void SceneAudio::Update()
	{
		// Listener follows the primary camera.
		if (Entity camera = m_Scene->GetPrimaryCameraEntity())
		{
			const TransformComponent& transform = camera.GetTransform();
			AudioEngine::SetListener(transform.Translation, transform.GetForward(), transform.GetUp());
		}

		std::vector<UUID> stale;
		for (auto& [uuid, instance] : m_Sounds)
		{
			Entity entity = m_Scene->GetEntityByUUID(uuid);
			const AudioSourceComponent* source = entity ? entity.TryGetComponent<AudioSourceComponent>() : nullptr;
			if (!source || source->Clip != instance->Clip)
			{
				stale.push_back(uuid);
				continue;
			}

			ma_sound_set_volume(&instance->Sound, source->Volume);
			ma_sound_set_pitch(&instance->Sound, std::max(source->Pitch, 0.01f));
			ma_sound_set_looping(&instance->Sound, source->Loop ? MA_TRUE : MA_FALSE);
			ma_sound_set_spatialization_enabled(&instance->Sound, source->Spatial ? MA_TRUE : MA_FALSE);
			if (source->Spatial)
			{
				const glm::vec3& position = entity.GetTransform().Translation;
				ma_sound_set_position(&instance->Sound, position.x, position.y, position.z);
			}
		}

		for (UUID uuid : stale)
			ReleaseSound(uuid);
	}

	void SceneAudio::Stop()
	{
		m_Sounds.clear();
	}

	void SceneAudio::OnEntityDestroyed(Entity entity)
	{
		ReleaseSound(entity.GetUUID());
	}

	bool SceneAudio::Play(Entity entity)
	{
		SoundInstance* instance = GetOrCreateSound(entity);
		if (!instance)
			return false;

		const AudioSourceComponent& source = entity.GetComponent<AudioSourceComponent>();
		ma_sound_set_volume(&instance->Sound, source.Volume);
		ma_sound_set_pitch(&instance->Sound, std::max(source.Pitch, 0.01f));
		ma_sound_set_looping(&instance->Sound, source.Loop ? MA_TRUE : MA_FALSE);
		ma_sound_set_spatialization_enabled(&instance->Sound, source.Spatial ? MA_TRUE : MA_FALSE);
		ma_sound_seek_to_pcm_frame(&instance->Sound, 0);
		return ma_sound_start(&instance->Sound) == MA_SUCCESS;
	}

	void SceneAudio::StopSource(Entity entity)
	{
		if (auto it = m_Sounds.find(entity.GetUUID()); it != m_Sounds.end())
			ma_sound_stop(&it->second->Sound);
	}

	bool SceneAudio::IsPlaying(Entity entity) const
	{
		if (auto it = m_Sounds.find(entity.GetUUID()); it != m_Sounds.end())
			return ma_sound_is_playing(&it->second->Sound) == MA_TRUE;
		return false;
	}

}
