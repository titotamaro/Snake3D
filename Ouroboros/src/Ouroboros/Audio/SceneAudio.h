#pragma once

#include "Ouroboros/Core/Base.h"
#include "Ouroboros/Core/UUID.h"

#include <unordered_map>

struct ma_sound;

namespace Ouroboros {

	class Entity;
	class Scene;

	// Runtime playback state for a scene's AudioSource components. Sounds are created lazily for each AudioSource and
	// kept in sync with the component (volume, pitch, looping, spatial position) every frame.
	class SceneAudio
	{
	public:
		explicit SceneAudio(Scene* scene);
		~SceneAudio();

		SceneAudio(const SceneAudio&) = delete;
		SceneAudio& operator=(const SceneAudio&) = delete;

		// Starts every AudioSource with PlayOnStart.
		void Start();
		// Syncs sound parameters and the listener (primary camera). Releases sounds of removed sources.
		void Update();
		// Stops and releases every sound.
		void Stop();
		void OnEntityDestroyed(Entity entity);

		bool Play(Entity entity);
		void StopSource(Entity entity);
		bool IsPlaying(Entity entity) const;
		size_t GetActiveSoundCount() const { return m_Sounds.size(); }

	private:
		struct SoundInstance;
		SoundInstance* GetOrCreateSound(Entity entity);
		void ReleaseSound(UUID uuid);

	private:
		Scene* m_Scene;
		std::unordered_map<UUID, Scope<SoundInstance>> m_Sounds;
	};

}
