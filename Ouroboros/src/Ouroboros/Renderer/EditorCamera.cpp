#include "obpch.h"
#include "Ouroboros/Renderer/EditorCamera.h"

#include "Ouroboros/Core/Input.h"
#include "Ouroboros/Math/Math.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Ouroboros {

	EditorCamera::EditorCamera(float fovDegrees, float nearClip, float farClip)
		: m_FieldOfView(glm::radians(fovDegrees)), m_Near(nearClip), m_Far(farClip)
	{
		UpdateView();
	}

	void EditorCamera::OnUpdate(Timestep ts, bool acceptInput)
	{
		if (acceptInput)
		{
			const glm::vec2 delta = Input::GetMouseDelta() * 0.003f;
			if (Input::IsMouseButtonDown(Mouse::ButtonRight))
			{
				m_Yaw -= delta.x * 2.0f;
				m_Pitch = glm::clamp(m_Pitch + delta.y * 2.0f, -1.55f, 1.55f);
			}
			else if (Input::IsMouseButtonDown(Mouse::ButtonMiddle))
			{
				const float speed = m_Distance * 0.6f;
				m_FocalPoint += (-GetRight() * delta.x + GetUp() * delta.y) * speed;
			}

			const float scroll = Input::GetScrollDelta().y;
			if (scroll != 0.0f)
				m_Distance = glm::clamp(m_Distance * (1.0f - scroll * 0.1f), 0.5f, m_Far * 0.5f);

			// WASD fly while orbiting with the right mouse button.
			if (Input::IsMouseButtonDown(Mouse::ButtonRight))
			{
				const float speed = std::max(m_Distance, 2.0f) * ts.GetSeconds();
				glm::vec3 move(0.0f);
				if (Input::IsKeyDown(Key::W))
					move += GetForward();
				if (Input::IsKeyDown(Key::S))
					move -= GetForward();
				if (Input::IsKeyDown(Key::D))
					move += GetRight();
				if (Input::IsKeyDown(Key::A))
					move -= GetRight();
				m_FocalPoint += move * speed;
			}
		}
		UpdateView();
	}

	void EditorCamera::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (width > 0 && height > 0)
			m_AspectRatio = static_cast<float>(width) / static_cast<float>(height);
	}

	void EditorCamera::LookAt(const glm::vec3& position, const glm::vec3& target)
	{
		glm::vec3 offset = position - target;
		m_Distance = std::max(glm::length(offset), 0.01f);
		offset /= m_Distance;
		m_FocalPoint = target;
		m_Pitch = std::asin(glm::clamp(offset.y, -1.0f, 1.0f));
		m_Yaw = std::atan2(offset.x, offset.z);
		UpdateView();
	}

	void EditorCamera::Focus(const glm::vec3& target, float distance)
	{
		m_FocalPoint = target;
		m_Distance = std::max(distance, 0.5f);
		UpdateView();
	}

	glm::mat4 EditorCamera::GetProjection() const
	{
		return Math::PerspectiveProjection(m_FieldOfView, m_AspectRatio, m_Near, m_Far);
	}

	glm::vec3 EditorCamera::GetPosition() const
	{
		const glm::vec3 offset(std::cos(m_Pitch) * std::sin(m_Yaw), std::sin(m_Pitch), std::cos(m_Pitch) * std::cos(m_Yaw));
		return m_FocalPoint + offset * m_Distance;
	}

	glm::vec3 EditorCamera::GetForward() const
	{
		return glm::normalize(m_FocalPoint - GetPosition());
	}

	glm::vec3 EditorCamera::GetRight() const
	{
		return glm::normalize(glm::cross(GetForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
	}

	glm::vec3 EditorCamera::GetUp() const
	{
		return glm::cross(GetRight(), GetForward());
	}

	void EditorCamera::UpdateView()
	{
		m_View = glm::lookAtRH(GetPosition(), m_FocalPoint, glm::vec3(0.0f, 1.0f, 0.0f));
	}

}
