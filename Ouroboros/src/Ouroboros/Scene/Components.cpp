#include "obpch.h"
#include "Ouroboros/Scene/Components.h"

namespace Ouroboros {

	glm::mat4 CameraComponent::GetProjection(float aspectRatio) const
	{
		if (aspectRatio <= 0.0f)
			aspectRatio = 1.0f;

		if (Projection == ProjectionType::Perspective)
			return Math::PerspectiveProjection(PerspectiveFOV, aspectRatio, PerspectiveNear, PerspectiveFar);

		const float halfHeight = OrthographicSize * 0.5f;
		const float halfWidth = halfHeight * aspectRatio;
		return Math::OrthographicProjection(-halfWidth, halfWidth, -halfHeight, halfHeight, OrthographicNear, OrthographicFar);
	}

}
