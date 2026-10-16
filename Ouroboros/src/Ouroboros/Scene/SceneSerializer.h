#pragma once

#include "Ouroboros/Scene/Scene.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace Ouroboros {

	class Entity;

	// Reads and writes scenes as JSON (.oscene). The format is documented in AGENTS.md.
	class SceneSerializer
	{
	public:
		static constexpr int FormatVersion = 1;

		explicit SceneSerializer(const Ref<Scene>& scene);

		nlohmann::json Serialize() const;
		// Replaces the scene contents. On failure the scene is left empty and `error` describes the problem.
		bool Deserialize(const nlohmann::json& data, std::string* error = nullptr);

		bool SaveToFile(const std::filesystem::path& path) const;
		bool LoadFromFile(const std::filesystem::path& path, std::string* error = nullptr);

		static nlohmann::json SerializeEntity(Entity entity);
		// Creates an entity from its serialized form. A fresh UUID is generated if `keepUUID` is false or it clashes.
		static Entity DeserializeEntity(Scene& scene, const nlohmann::json& data, bool keepUUID = true);

	private:
		Ref<Scene> m_Scene;
	};

}
