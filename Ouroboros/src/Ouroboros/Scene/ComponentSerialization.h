#pragma once

#include "Ouroboros/Scene/Components.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string_view>

namespace nlohmann {

	template<>
	struct adl_serializer<glm::vec2>
	{
		static void to_json(json& j, const glm::vec2& value) { j = json::array({ value.x, value.y }); }
		static void from_json(const json& j, glm::vec2& value);
	};

	template<>
	struct adl_serializer<glm::vec3>
	{
		static void to_json(json& j, const glm::vec3& value) { j = json::array({ value.x, value.y, value.z }); }
		static void from_json(const json& j, glm::vec3& value);
	};

	template<>
	struct adl_serializer<glm::vec4>
	{
		static void to_json(json& j, const glm::vec4& value) { j = json::array({ value.x, value.y, value.z, value.w }); }
		static void from_json(const json& j, glm::vec4& value);
	};

}

namespace Ouroboros {

	// JSON conversion for every component. `from_json` only overwrites the fields present in the JSON object, so partial
	// objects can be used to patch a component (this is what the agent command API's `component.set` relies on).
	// Invalid values throw nlohmann::json::exception.

	void to_json(nlohmann::json& j, const TagComponent& component);
	void from_json(const nlohmann::json& j, TagComponent& component);

	void to_json(nlohmann::json& j, const TransformComponent& component);
	void from_json(const nlohmann::json& j, TransformComponent& component);

	void to_json(nlohmann::json& j, const CameraComponent& component);
	void from_json(const nlohmann::json& j, CameraComponent& component);

	void to_json(nlohmann::json& j, const MeshRendererComponent& component);
	void from_json(const nlohmann::json& j, MeshRendererComponent& component);

	void to_json(nlohmann::json& j, const DirectionalLightComponent& component);
	void from_json(const nlohmann::json& j, DirectionalLightComponent& component);

	void to_json(nlohmann::json& j, const PointLightComponent& component);
	void from_json(const nlohmann::json& j, PointLightComponent& component);

	void to_json(nlohmann::json& j, const SkyLightComponent& component);
	void from_json(const nlohmann::json& j, SkyLightComponent& component);

	void to_json(nlohmann::json& j, const RigidBodyComponent& component);
	void from_json(const nlohmann::json& j, RigidBodyComponent& component);

	void to_json(nlohmann::json& j, const BoxColliderComponent& component);
	void from_json(const nlohmann::json& j, BoxColliderComponent& component);

	void to_json(nlohmann::json& j, const SphereColliderComponent& component);
	void from_json(const nlohmann::json& j, SphereColliderComponent& component);

	void to_json(nlohmann::json& j, const ScriptComponent& component);
	void from_json(const nlohmann::json& j, ScriptComponent& component);

	void to_json(nlohmann::json& j, const AudioSourceComponent& component);
	void from_json(const nlohmann::json& j, AudioSourceComponent& component);

	const char* ProjectionTypeToString(ProjectionType type);
	std::optional<ProjectionType> ProjectionTypeFromString(std::string_view name);
	const char* RigidBodyTypeToString(RigidBodyType type);
	std::optional<RigidBodyType> RigidBodyTypeFromString(std::string_view name);

}
