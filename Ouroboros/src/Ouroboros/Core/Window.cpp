#include "obpch.h"
#include "Ouroboros/Core/Window.h"

#include "Ouroboros/Core/Input.h"

#include <GLFW/glfw3.h>

namespace Ouroboros {

	static uint32_t s_GLFWWindowCount = 0;

	static void GLFWErrorCallback(int error, const char* description)
	{
		OB_CORE_ERROR("GLFW error ({}): {}", error, description);
	}

	Window::Window(const WindowSpecification& specification)
	{
		m_Data.Title = specification.Title;

		if (s_GLFWWindowCount == 0)
		{
			glfwSetErrorCallback(GLFWErrorCallback);
			const int success = glfwInit();
			OB_CORE_VERIFY(success, "Could not initialize GLFW");
		}

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, specification.Resizable ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_MAXIMIZED, specification.Maximized ? GLFW_TRUE : GLFW_FALSE);

		GLFWmonitor* monitor = nullptr;
		int width = static_cast<int>(specification.Width);
		int height = static_cast<int>(specification.Height);
		if (specification.Fullscreen)
		{
			monitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* mode = glfwGetVideoMode(monitor);
			width = mode->width;
			height = mode->height;
		}

		m_Window = glfwCreateWindow(width, height, m_Data.Title.c_str(), monitor, nullptr);
		OB_CORE_VERIFY(m_Window, "Could not create GLFW window");
		s_GLFWWindowCount++;

		OB_CORE_INFO("Created window '{}' ({} x {})", m_Data.Title, width, height);

		glfwSetWindowUserPointer(m_Window, &m_Data);

		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int newWidth, int newHeight)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			WindowResizeEvent event(static_cast<uint32_t>(newWidth), static_cast<uint32_t>(newHeight));
			if (data.EventCallback)
				data.EventCallback(event);
		});

		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			WindowCloseEvent event;
			if (data.EventCallback)
				data.EventCallback(event);
		});

		glfwSetWindowFocusCallback(m_Window, [](GLFWwindow* window, int focused)
		{
			if (!focused)
				Input::Reset();

			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			WindowFocusEvent event(focused == GLFW_TRUE);
			if (data.EventCallback)
				data.EventCallback(event);
		});

		glfwSetDropCallback(m_Window, [](GLFWwindow* window, int count, const char** paths)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			std::vector<std::string> droppedPaths(paths, paths + count);
			WindowDropEvent event(std::move(droppedPaths));
			if (data.EventCallback)
				data.EventCallback(event);
		});

		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
		{
			if (key < 0)
				return;

			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			const KeyCode keyCode = static_cast<KeyCode>(key);
			switch (action)
			{
				case GLFW_PRESS:
				{
					Input::OnKey(keyCode, true);
					KeyPressedEvent event(keyCode, false);
					if (data.EventCallback)
						data.EventCallback(event);
					break;
				}
				case GLFW_RELEASE:
				{
					Input::OnKey(keyCode, false);
					KeyReleasedEvent event(keyCode);
					if (data.EventCallback)
						data.EventCallback(event);
					break;
				}
				case GLFW_REPEAT:
				{
					KeyPressedEvent event(keyCode, true);
					if (data.EventCallback)
						data.EventCallback(event);
					break;
				}
				default:
					break;
			}
		});

		glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int codepoint)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			KeyTypedEvent event(codepoint);
			if (data.EventCallback)
				data.EventCallback(event);
		});

		glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int /*mods*/)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			const MouseCode mouseCode = static_cast<MouseCode>(button);
			if (action == GLFW_PRESS)
			{
				Input::OnMouseButton(mouseCode, true);
				MouseButtonPressedEvent event(mouseCode);
				if (data.EventCallback)
					data.EventCallback(event);
			}
			else if (action == GLFW_RELEASE)
			{
				Input::OnMouseButton(mouseCode, false);
				MouseButtonReleasedEvent event(mouseCode);
				if (data.EventCallback)
					data.EventCallback(event);
			}
		});

		glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xOffset, double yOffset)
		{
			Input::OnScroll(static_cast<float>(xOffset), static_cast<float>(yOffset));
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			MouseScrolledEvent event(static_cast<float>(xOffset), static_cast<float>(yOffset));
			if (data.EventCallback)
				data.EventCallback(event);
		});

		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double x, double y)
		{
			Input::OnMouseMove(static_cast<float>(x), static_cast<float>(y));
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			MouseMovedEvent event(static_cast<float>(x), static_cast<float>(y));
			if (data.EventCallback)
				data.EventCallback(event);
		});
	}

	Window::~Window()
	{
		glfwDestroyWindow(m_Window);
		s_GLFWWindowCount--;
		if (s_GLFWWindowCount == 0)
			glfwTerminate();
	}

	void Window::PollEvents()
	{
		glfwPollEvents();
	}

	uint32_t Window::GetWidth() const
	{
		int width = 0, height = 0;
		glfwGetFramebufferSize(m_Window, &width, &height);
		return static_cast<uint32_t>(width);
	}

	uint32_t Window::GetHeight() const
	{
		int width = 0, height = 0;
		glfwGetFramebufferSize(m_Window, &width, &height);
		return static_cast<uint32_t>(height);
	}

	bool Window::IsMinimized() const
	{
		return GetWidth() == 0 || GetHeight() == 0 || glfwGetWindowAttrib(m_Window, GLFW_ICONIFIED);
	}

	void Window::SetTitle(const std::string& title)
	{
		m_Data.Title = title;
		glfwSetWindowTitle(m_Window, title.c_str());
	}

}
