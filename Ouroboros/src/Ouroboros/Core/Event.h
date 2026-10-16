#pragma once

#include "Ouroboros/Core/KeyCodes.h"

#include <string>
#include <vector>

namespace Ouroboros {

	enum class EventType
	{
		None = 0,
		WindowClose,
		WindowResize,
		WindowFocus,
		WindowDrop,
		KeyPressed,
		KeyReleased,
		KeyTyped,
		MouseButtonPressed,
		MouseButtonReleased,
		MouseMoved,
		MouseScrolled
	};

#define OB_EVENT_CLASS_TYPE(type)                                                \
	static EventType GetStaticType() { return EventType::type; }                \
	virtual EventType GetEventType() const override { return GetStaticType(); } \
	virtual const char* GetName() const override { return #type; }

	class Event
	{
	public:
		virtual ~Event() = default;

		virtual EventType GetEventType() const = 0;
		virtual const char* GetName() const = 0;

		bool Handled = false;
	};

	class EventDispatcher
	{
	public:
		explicit EventDispatcher(Event& event)
			: m_Event(event)
		{
		}

		// Calls `func` when the event is of type T. `func` returns true when it consumed the event.
		template<typename T, typename F>
		bool Dispatch(const F& func)
		{
			if (m_Event.GetEventType() != T::GetStaticType())
				return false;
			m_Event.Handled |= func(static_cast<T&>(m_Event));
			return true;
		}

	private:
		Event& m_Event;
	};

	class WindowCloseEvent : public Event
	{
	public:
		OB_EVENT_CLASS_TYPE(WindowClose)
	};

	class WindowResizeEvent : public Event
	{
	public:
		WindowResizeEvent(uint32_t width, uint32_t height)
			: m_Width(width), m_Height(height)
		{
		}

		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

		OB_EVENT_CLASS_TYPE(WindowResize)

	private:
		uint32_t m_Width, m_Height;
	};

	class WindowFocusEvent : public Event
	{
	public:
		explicit WindowFocusEvent(bool focused)
			: m_Focused(focused)
		{
		}

		bool IsFocused() const { return m_Focused; }

		OB_EVENT_CLASS_TYPE(WindowFocus)

	private:
		bool m_Focused;
	};

	class WindowDropEvent : public Event
	{
	public:
		explicit WindowDropEvent(std::vector<std::string> paths)
			: m_Paths(std::move(paths))
		{
		}

		const std::vector<std::string>& GetPaths() const { return m_Paths; }

		OB_EVENT_CLASS_TYPE(WindowDrop)

	private:
		std::vector<std::string> m_Paths;
	};

	class KeyPressedEvent : public Event
	{
	public:
		KeyPressedEvent(KeyCode keyCode, bool isRepeat)
			: m_KeyCode(keyCode), m_IsRepeat(isRepeat)
		{
		}

		KeyCode GetKeyCode() const { return m_KeyCode; }
		bool IsRepeat() const { return m_IsRepeat; }

		OB_EVENT_CLASS_TYPE(KeyPressed)

	private:
		KeyCode m_KeyCode;
		bool m_IsRepeat;
	};

	class KeyReleasedEvent : public Event
	{
	public:
		explicit KeyReleasedEvent(KeyCode keyCode)
			: m_KeyCode(keyCode)
		{
		}

		KeyCode GetKeyCode() const { return m_KeyCode; }

		OB_EVENT_CLASS_TYPE(KeyReleased)

	private:
		KeyCode m_KeyCode;
	};

	class KeyTypedEvent : public Event
	{
	public:
		explicit KeyTypedEvent(uint32_t codepoint)
			: m_Codepoint(codepoint)
		{
		}

		uint32_t GetCodepoint() const { return m_Codepoint; }

		OB_EVENT_CLASS_TYPE(KeyTyped)

	private:
		uint32_t m_Codepoint;
	};

	class MouseButtonPressedEvent : public Event
	{
	public:
		explicit MouseButtonPressedEvent(MouseCode button)
			: m_Button(button)
		{
		}

		MouseCode GetMouseButton() const { return m_Button; }

		OB_EVENT_CLASS_TYPE(MouseButtonPressed)

	private:
		MouseCode m_Button;
	};

	class MouseButtonReleasedEvent : public Event
	{
	public:
		explicit MouseButtonReleasedEvent(MouseCode button)
			: m_Button(button)
		{
		}

		MouseCode GetMouseButton() const { return m_Button; }

		OB_EVENT_CLASS_TYPE(MouseButtonReleased)

	private:
		MouseCode m_Button;
	};

	class MouseMovedEvent : public Event
	{
	public:
		MouseMovedEvent(float x, float y)
			: m_X(x), m_Y(y)
		{
		}

		float GetX() const { return m_X; }
		float GetY() const { return m_Y; }

		OB_EVENT_CLASS_TYPE(MouseMoved)

	private:
		float m_X, m_Y;
	};

	class MouseScrolledEvent : public Event
	{
	public:
		MouseScrolledEvent(float xOffset, float yOffset)
			: m_XOffset(xOffset), m_YOffset(yOffset)
		{
		}

		float GetXOffset() const { return m_XOffset; }
		float GetYOffset() const { return m_YOffset; }

		OB_EVENT_CLASS_TYPE(MouseScrolled)

	private:
		float m_XOffset, m_YOffset;
	};

}
