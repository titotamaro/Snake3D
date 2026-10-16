#pragma once

#include "Ouroboros/Core/UUID.h"
#include "Ouroboros/Math/Math.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <string>

namespace Ouroboros {

	// Every component is a plain data struct with public PascalCase fields.
	// Adding a component: declare it here, add JSON (de)serialization in ComponentSerialization.cpp, register it in
	// ComponentRegistry.cpp, expose it in ScriptGlue.cpp and draw it in the editor's InspectorPanel.

	struct IDComponent
	{
		UUID ID;
	};

	struct TagComponent
	{
		std::string Tag;
	};

	struct TransformComponent
	{
		glm::vec3 Translation{ 0.0f };
		glm::vec3 Rotation{ 0.0f }; // Euler angles in radians
		glm::vec3 Scale{ 1.0f };

		glm::mat4 GetTransform() const { return Math::ComposeTransform(Translation, Rotation, Scale); }
		glm::quat GetRotationQuat() const { return glm::quat(Rotation); }
		void SetRotationQuat(const glm::quat& rotation) { Rotation = glm::eulerAngles(rotation); }

		glm::vec3 GetForward() const { return GetRotationQuat() * glm::vec3(0.0f, 0.0f, -1.0f); }
		glm::vec3 GetRight() const { return GetRotationQuat() * glm::vec3(1.0f, 0.0f, 0.0f); }
		glm::vec3 GetUp() const { return GetRotationQuat() * glm::vec3(0.0f, 1.0f, 0.0f); }
	};

	enum class ProjectionType : uint8_t
	{
		Perspective = 0,
		Orthographic = 1
	};

	struct CameraComponent
	{
		ProjectionType Projection = ProjectionType::Perspective;
		float PerspectiveFOV = glm::radians(45.0f); // vertical, radians
		float PerspectiveNear = 0.1f;
		float PerspectiveFar = 500.0f;
		float OrthographicSize = 10.0f;
		float OrthographicNear = -100.0f;
		float OrthographicFar = 100.0f;
		float Exposure = 1.0f;
		bool Primary = true;

		glm::mat4 GetProjection(float aspectRatio) const;
		float GetNear() const { return Projection == ProjectionType::Perspective ? PerspectiveNear : OrthographicNear; }
		float GetFar() const { return Projection == ProjectionType::Perspective ? PerspectiveFar : OrthographicFar; }
	};

	struct MeshRendererComponent
	{
		// Asset-relative path to a .gltf/.glb file, or a built-in primitive: "Primitive:Cube", "Primitive:Sphere", "Primitive:Plane".
		std::string Mesh = "Primitive:Cube";
		glm::vec4 Color{ 1.0f };      // multiplies the material base color
		float Metallic = -1.0f;       // >= 0 overrides the material's metallic value, < 0 keeps it
		float Roughness = -1.0f;      // >= 0 overrides the material's roughness value, < 0 keeps it
		glm::vec3 Emissive{ 0.0f };   // added to the material emissive color
		float EmissiveIntensity = 1.0f;
		bool CastShadows = true;
		bool Visible = true;
	};

	struct DirectionalLightComponent
	{
		glm::vec3 Color{ 1.0f };
		float Intensity = 3.0f;
		bool CastShadows = true;
		float ShadowSoftness = 1.0f;   // light size used by the soft shadow filter
		float ShadowDistance = 60.0f;  // shadows are rendered up to this distance from the camera
	};

	struct PointLightComponent
	{
		glm::vec3 Color{ 1.0f };
		float Intensity = 10.0f;
		float Radius = 10.0f;
	};

	struct SkyLightComponent
	{
		std::string Environment;      // asset-relative path to an equirectangular .hdr
		float Intensity = 1.0f;
		float Rotation = 0.0f;        // radians around +Y
		bool ShowBackground = true;
	};

	enum class RigidBodyType : uint8_t
	{
		Static = 0,
		Dynamic = 1,
		Kinematic = 2
	};

	struct RigidBodyComponent
	{
		RigidBodyType Type = RigidBodyType::Dynamic;
		float Mass = 1.0f;
		float LinearDamping = 0.05f;
		float AngularDamping = 0.05f;
		float GravityFactor = 1.0f;
		bool FixedRotation = false;
	};

	struct BoxColliderComponent
	{
		glm::vec3 HalfExtents{ 0.5f };
		glm::vec3 Offset{ 0.0f };
		float Friction = 0.5f;
		float Restitution = 0.0f;
		bool IsTrigger = false;
	};

	struct SphereColliderComponent
	{
		float Radius = 0.5f;
		glm::vec3 Offset{ 0.0f };
		float Friction = 0.5f;
		float Restitution = 0.0f;
		bool IsTrigger = false;
	};

	struct ScriptComponent
	{
		std::string Script;                              // asset-relative path to a .lua file
		nlohmann::json Fields = nlohmann::json::object(); // overrides for the script's `Properties` table
	};

	struct AudioSourceComponent
	{
		std::string Clip;             // asset-relative path to a .wav/.mp3/.flac/.ogg file
		float Volume = 1.0f;
		float Pitch = 1.0f;
		bool Loop = false;
		bool PlayOnStart = false;
		bool Spatial = false;
	};

}
