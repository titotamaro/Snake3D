#pragma once

#include "Ouroboros/Core/Event.h"

#include <cstdint>
#include <functional>
#include <string>

struct GLFWwindow;

namespace Ouroboros {

	struct WindowSpecification
	{
		std::string Title = "Ouroboros";
		uint32_t Width = 1600;
		uint32_t Height = 900;
		bool Fullscreen = false;
		bool Resizable = true;
		bool Maximized = false;
	};

	class Window
	{
	public:
		using EventCallbackFn = std::function<void(Event&)>;

		explicit Window(const WindowSpecification& specification);
		~Window();

		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

		void PollEvents();

		uint32_t GetWidth() const;
		uint32_t GetHeight() const;
		bool IsMinimized() const;

		void SetTitle(const std::string& title);
		const std::string& GetTitle() const { return m_Data.Title; }

		void SetEventCallback(const EventCallbackFn& callback) { m_Data.EventCallback = callback; }

		GLFWwindow* GetNativeWindow() const { return m_Window; }

	private:
		GLFWwindow* m_Window = nullptr;

		struct WindowData
		{
			std::string Title;
			EventCallbackFn EventCallback;
		};

		WindowData m_Data;
	};

}
