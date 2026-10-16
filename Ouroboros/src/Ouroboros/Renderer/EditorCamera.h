#pragma once

#include "Ouroboros/Core/Timestep.h"

#include <glm/glm.hpp>

namespace Ouroboros {

	// Orbit camera used by the editor viewport.
	// Controls: right mouse = orbit, middle mouse = pan, scroll = zoom, F = focus (handled by the editor).
	class EditorCamera
	{
	public:
		EditorCamera(float fovDegrees = 45.0f, float nearClip = 0.1f, float farClip = 1000.0f);

		// Applies mouse input when `acceptInput` is true (viewport hovered/focused).
		void OnUpdate(Timestep ts, bool acceptInput);

		void SetViewportSize(uint32_t width, uint32_t height);
		// Positions the camera at `position` looking at `target`.
		void LookAt(const glm::vec3& position, const glm::vec3& target);
		void Focus(const glm::vec3& target, float distance);

		const glm::mat4& GetView() const { return m_View; }
		glm::mat4 GetProjection() const;
		glm::vec3 GetPosition() const;
		const glm::vec3& GetFocalPoint() const { return m_FocalPoint; }
		float GetDistance() const { return m_Distance; }
		float GetPitch() const { return m_Pitch; }
		float GetYaw() const { return m_Yaw; }
		float GetNear() const { return m_Near; }
		float GetFar() const { return m_Far; }
		float GetAspectRatio() const { return m_AspectRatio; }

		glm::vec3 GetForward() const;
		glm::vec3 GetRight() const;
		glm::vec3 GetUp() const;

	private:
		void UpdateView();

	private:
		float m_FieldOfView;
		float m_Near;
		float m_Far;
		float m_AspectRatio = 16.0f / 9.0f;

		glm::vec3 m_FocalPoint{ 0.0f };
		float m_Distance = 15.0f;
		float m_Pitch = 0.6f;
		float m_Yaw = -0.6f;

		glm::mat4 m_View{ 1.0f };
	};

}
