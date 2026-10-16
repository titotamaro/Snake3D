#include "obpch.h"
#include "Ouroboros/Math/Math.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Ouroboros::Math {

	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::vec3& rotation, glm::vec3& scale)
	{
		glm::vec3 skew;
		glm::vec4 perspective;
		glm::quat orientation;
		if (!glm::decompose(transform, scale, orientation, translation, skew, perspective))
			return false;

		if (glm::abs(perspective.x) > 1e-5f || glm::abs(perspective.y) > 1e-5f || glm::abs(perspective.z) > 1e-5f)
			return false;

		rotation = glm::eulerAngles(orientation);
		return true;
	}

	glm::mat4 ComposeTransform(const glm::vec3& translation, const glm::vec3& rotation, const glm::vec3& scale)
	{
		return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(glm::quat(rotation)) * glm::scale(glm::mat4(1.0f), scale);
	}

	glm::mat3 NormalMatrix(const glm::mat4& transform)
	{
		const glm::mat3 upper(transform);
		const float determinant = glm::determinant(upper);
		if (glm::abs(determinant) < 1e-12f)
			return glm::mat3(1.0f);
		return glm::transpose(glm::inverse(upper));
	}

	glm::mat4 PerspectiveProjection(float fovY, float aspectRatio, float nearClip, float farClip)
	{
		return glm::perspectiveRH_ZO(fovY, aspectRatio, nearClip, farClip);
	}

	glm::mat4 OrthographicProjection(float left, float right, float bottom, float top, float nearClip, float farClip)
	{
		return glm::orthoRH_ZO(left, right, bottom, top, nearClip, farClip);
	}

}
