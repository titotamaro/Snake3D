#include "obpch.h"
#include "Ouroboros/Scene/ComponentSerialization.h"

namespace nlohmann {

	namespace {

		void ExpectNumberArray(const json& j, size_t size)
		{
			if (!j.is_array() || j.size() != size)
				throw json::type_error::create(302, "expected an array of " + std::to_string(size) + " numbers", &j);
			for (const json& element : j)
			{
				if (!element.is_number())
					throw json::type_error::create(302, "expected an array of " + std::to_string(size) + " numbers", &j);
			}
		}

	}

	void adl_serializer<glm::vec2>::from_json(const json& j, glm::vec2& value)
	{
		ExpectNumberArray(j, 2);
		value = glm::vec2(j[0].get<float>(), j[1].get<float>());
	}

	void adl_serializer<glm::vec3>::from_json(const json& j, glm::vec3& value)
	{
		ExpectNumberArray(j, 3);
		value = glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
	}

	void adl_serializer<glm::vec4>::from_json(const json& j, glm::vec4& value)
	{
		ExpectNumberArray(j, 4);
		value = glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
	}

}

namespace Ouroboros {

	using json = nlohmann::json;

	namespace {

		void ExpectObject(const json& j, const char* componentName)
		{
			if (!j.is_object())
				throw json::type_error::create(302, std::string(componentName) + " must be a JSON object", &j);
		}

		template<typename T>
		void ReadField(const json& j, const char* key, T& value)
		{
			if (auto it = j.find(key); it != j.end())
				value = it->template get<T>();
		}

	}

	const char* ProjectionTypeToString(ProjectionType type)
	{
		switch (type)
		{
			case ProjectionType::Perspective:
				return "Perspective";
			case ProjectionType::Orthographic:
				return "Orthographic";
		}
		return "Perspective";
	}

	std::optional<ProjectionType> ProjectionTypeFromString(std::string_view name)
	{
		if (name == "Perspective")
			return ProjectionType::Perspective;
		if (name == "Orthographic")
			return ProjectionType::Orthographic;
		return std::nullopt;
	}

	const char* RigidBodyTypeToString(RigidBodyType type)
	{
		switch (type)
		{
			case RigidBodyType::Static:
				return "Static";
			case RigidBodyType::Dynamic:
				return "Dynamic";
			case RigidBodyType::Kinematic:
				return "Kinematic";
		}
		return "Static";
	}

	std::optional<RigidBodyType> RigidBodyTypeFromString(std::string_view name)
	{
		if (name == "Static")
			return RigidBodyType::Static;
		if (name == "Dynamic")
			return RigidBodyType::Dynamic;
		if (name == "Kinematic")
			return RigidBodyType::Kinematic;
		return std::nullopt;
	}

	// Tag ---------------------------------------------------------------------------------------------------------
	void to_json(json& j, const TagComponent& component)
	{
		j = json{ { "Tag", component.Tag } };
	}

	void from_json(const json& j, TagComponent& component)
	{
		ExpectObject(j, "Tag");
		ReadField(j, "Tag", component.Tag);
	}

	// Transform ---------------------------------------------------------------------------------------------------
	void to_json(json& j, const TransformComponent& component)
	{
		j = json{
			{ "Translation", component.Translation },
			{ "Rotation", component.Rotation },
			{ "Scale", component.Scale }
		};
	}

	void from_json(const json& j, TransformComponent& component)
	{
		ExpectObject(j, "Transform");
		ReadField(j, "Translation", component.Translation);
		ReadField(j, "Rotation", component.Rotation);
		ReadField(j, "Scale", component.Scale);
	}

	// Camera ------------------------------------------------------------------------------------------------------
	void to_json(json& j, const CameraComponent& component)
	{
		j = json{
			{ "Projection", ProjectionTypeToString(component.Projection) },
			{ "PerspectiveFOV", component.PerspectiveFOV },
			{ "PerspectiveNear", component.PerspectiveNear },
			{ "PerspectiveFar", component.PerspectiveFar },
			{ "OrthographicSize", component.OrthographicSize },
			{ "OrthographicNear", component.OrthographicNear },
			{ "OrthographicFar", component.OrthographicFar },
			{ "Exposure", component.Exposure },
			{ "Primary", component.Primary }
		};
	}

	void from_json(const json& j, CameraComponent& component)
	{
		ExpectObject(j, "Camera");
		if (auto it = j.find("Projection"); it != j.end())
		{
			const auto projection = ProjectionTypeFromString(it->get<std::string>());
			if (!projection)
				throw json::other_error::create(501, "unknown camera projection '" + it->get<std::string>() + "'", &j);
			component.Projection = *projection;
		}
		ReadField(j, "PerspectiveFOV", component.PerspectiveFOV);
		ReadField(j, "PerspectiveNear", component.PerspectiveNear);
		ReadField(j, "PerspectiveFar", component.PerspectiveFar);
		ReadField(j, "OrthographicSize", component.OrthographicSize);
		ReadField(j, "OrthographicNear", component.OrthographicNear);
		ReadField(j, "OrthographicFar", component.OrthographicFar);
		ReadField(j, "Exposure", component.Exposure);
		ReadField(j, "Primary", component.Primary);
	}

	// MeshRenderer ------------------------------------------------------------------------------------------------
	void to_json(json& j, const MeshRendererComponent& component)
	{
		j = json{
			{ "Mesh", component.Mesh },
			{ "Color", component.Color },
			{ "Metallic", component.Metallic },
			{ "Roughness", component.Roughness },
			{ "Emissive", component.Emissive },
			{ "EmissiveIntensity", component.EmissiveIntensity },
			{ "CastShadows", component.CastShadows },
			{ "Visible", component.Visible }
		};
	}

	void from_json(const json& j, MeshRendererComponent& component)
	{
		ExpectObject(j, "MeshRenderer");
		ReadField(j, "Mesh", component.Mesh);
		ReadField(j, "Color", component.Color);
		ReadField(j, "Metallic", component.Metallic);
		ReadField(j, "Roughness", component.Roughness);
		ReadField(j, "Emissive", component.Emissive);
		ReadField(j, "EmissiveIntensity", component.EmissiveIntensity);
		ReadField(j, "CastShadows", component.CastShadows);
		ReadField(j, "Visible", component.Visible);
	}

	// DirectionalLight --------------------------------------------------------------------------------------------
	void to_json(json& j, const DirectionalLightComponent& component)
	{
		j = json{
			{ "Color", component.Color },
			{ "Intensity", component.Intensity },
			{ "CastShadows", component.CastShadows },
			{ "ShadowSoftness", component.ShadowSoftness },
			{ "ShadowDistance", component.ShadowDistance }
		};
	}

	void from_json(const json& j, DirectionalLightComponent& component)
	{
		ExpectObject(j, "DirectionalLight");
		ReadField(j, "Color", component.Color);
		ReadField(j, "Intensity", component.Intensity);
		ReadField(j, "CastShadows", component.CastShadows);
		ReadField(j, "ShadowSoftness", component.ShadowSoftness);
		ReadField(j, "ShadowDistance", component.ShadowDistance);
	}

	// PointLight --------------------------------------------------------------------------------------------------
	void to_json(json& j, const PointLightComponent& component)
	{
		j = json{
			{ "Color", component.Color },
			{ "Intensity", component.Intensity },
			{ "Radius", component.Radius }
		};
	}

	void from_json(const json& j, PointLightComponent& component)
	{
		ExpectObject(j, "PointLight");
		ReadField(j, "Color", component.Color);
		ReadField(j, "Intensity", component.Intensity);
		ReadField(j, "Radius", component.Radius);
	}

	// SkyLight ----------------------------------------------------------------------------------------------------
	void to_json(json& j, const SkyLightComponent& component)
	{
		j = json{
			{ "Environment", component.Environment },
			{ "Intensity", component.Intensity },
			{ "Rotation", component.Rotation },
			{ "ShowBackground", component.ShowBackground }
		};
	}

	void from_json(const json& j, SkyLightComponent& component)
	{
		ExpectObject(j, "SkyLight");
		ReadField(j, "Environment", component.Environment);
		ReadField(j, "Intensity", component.Intensity);
		ReadField(j, "Rotation", component.Rotation);
		ReadField(j, "ShowBackground", component.ShowBackground);
	}

	// RigidBody ---------------------------------------------------------------------------------------------------
	void to_json(json& j, const RigidBodyComponent& component)
	{
		j = json{
			{ "Type", RigidBodyTypeToString(component.Type) },
			{ "Mass", component.Mass },
			{ "LinearDamping", component.LinearDamping },
			{ "AngularDamping", component.AngularDamping },
			{ "GravityFactor", component.GravityFactor },
			{ "FixedRotation", component.FixedRotation }
		};
	}

	void from_json(const json& j, RigidBodyComponent& component)
	{
		ExpectObject(j, "RigidBody");
		if (auto it = j.find("Type"); it != j.end())
		{
			const auto type = RigidBodyTypeFromString(it->get<std::string>());
			if (!type)
				throw json::other_error::create(501, "unknown rigid body type '" + it->get<std::string>() + "'", &j);
			component.Type = *type;
		}
		ReadField(j, "Mass", component.Mass);
		ReadField(j, "LinearDamping", component.LinearDamping);
		ReadField(j, "AngularDamping", component.AngularDamping);
		ReadField(j, "GravityFactor", component.GravityFactor);
		ReadField(j, "FixedRotation", component.FixedRotation);
	}

	// BoxCollider -------------------------------------------------------------------------------------------------
	void to_json(json& j, const BoxColliderComponent& component)
	{
		j = json{
			{ "HalfExtents", component.HalfExtents },
			{ "Offset", component.Offset },
			{ "Friction", component.Friction },
			{ "Restitution", component.Restitution },
			{ "IsTrigger", component.IsTrigger }
		};
	}

	void from_json(const json& j, BoxColliderComponent& component)
	{
		ExpectObject(j, "BoxCollider");
		ReadField(j, "HalfExtents", component.HalfExtents);
		ReadField(j, "Offset", component.Offset);
		ReadField(j, "Friction", component.Friction);
		ReadField(j, "Restitution", component.Restitution);
		ReadField(j, "IsTrigger", component.IsTrigger);
	}

	// SphereCollider ----------------------------------------------------------------------------------------------
	void to_json(json& j, const SphereColliderComponent& component)
	{
		j = json{
			{ "Radius", component.Radius },
			{ "Offset", component.Offset },
			{ "Friction", component.Friction },
			{ "Restitution", component.Restitution },
			{ "IsTrigger", component.IsTrigger }
		};
	}

	void from_json(const json& j, SphereColliderComponent& component)
	{
		ExpectObject(j, "SphereCollider");
		ReadField(j, "Radius", component.Radius);
		ReadField(j, "Offset", component.Offset);
		ReadField(j, "Friction", component.Friction);
		ReadField(j, "Restitution", component.Restitution);
		ReadField(j, "IsTrigger", component.IsTrigger);
	}

	// Script ------------------------------------------------------------------------------------------------------
	void to_json(json& j, const ScriptComponent& component)
	{
		j = json{
			{ "Script", component.Script },
			{ "Fields", component.Fields }
		};
	}

	void from_json(const json& j, ScriptComponent& component)
	{
		ExpectObject(j, "Script");
		ReadField(j, "Script", component.Script);
		if (auto it = j.find("Fields"); it != j.end())
		{
			if (!it->is_object())
				throw json::type_error::create(302, "Script.Fields must be a JSON object", &j);
			// Fields are merged so a single field can be patched without resending the others.
			for (auto field = it->begin(); field != it->end(); ++field)
			{
				if (field.value().is_null())
					component.Fields.erase(field.key());
				else
					component.Fields[field.key()] = field.value();
			}
		}
	}

	// AudioSource -------------------------------------------------------------------------------------------------
	void to_json(json& j, const AudioSourceComponent& component)
	{
		j = json{
			{ "Clip", component.Clip },
			{ "Volume", component.Volume },
			{ "Pitch", component.Pitch },
			{ "Loop", component.Loop },
			{ "PlayOnStart", component.PlayOnStart },
			{ "Spatial", component.Spatial }
		};
	}

	void from_json(const json& j, AudioSourceComponent& component)
	{
		ExpectObject(j, "AudioSource");
		ReadField(j, "Clip", component.Clip);
		ReadField(j, "Volume", component.Volume);
		ReadField(j, "Pitch", component.Pitch);
		ReadField(j, "Loop", component.Loop);
		ReadField(j, "PlayOnStart", component.PlayOnStart);
		ReadField(j, "Spatial", component.Spatial);
	}

}
