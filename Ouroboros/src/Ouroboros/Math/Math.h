#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Ouroboros::Math {

	// Splits an affine transform into translation, euler rotation (radians, XYZ order) and scale.
	// Returns false if the matrix is degenerate (zero scale or projective).
	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::vec3& rotation, glm::vec3& scale);

	glm::mat4 ComposeTransform(const glm::vec3& translation, const glm::vec3& rotation, const glm::vec3& scale);

	// Builds an inverse-transpose 3x3 normal matrix for the upper 3x3 part of `transform`.
	glm::mat3 NormalMatrix(const glm::mat4& transform);

	// Right-handed projections with depth in [0, 1] and clip-space Y up (NVRHI applies D3D viewport conventions on
	// Vulkan, so no Y flip is needed). `fovY` is in radians.
	glm::mat4 PerspectiveProjection(float fovY, float aspectRatio, float nearClip, float farClip);
	glm::mat4 OrthographicProjection(float left, float right, float bottom, float top, float nearClip, float farClip);

	constexpr float Pi = 3.14159265358979323846f;

}
