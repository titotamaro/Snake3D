#include "obpch.h"
#include "Ouroboros/Core/Input.h"

#include <bitset>

namespace Ouroboros {

	namespace {

		struct InputState
		{
			std::bitset<Key::MaxKeyCode> KeysDown;
			std::bitset<Key::MaxKeyCode> KeysPressed;
			std::bitset<Key::MaxKeyCode> KeysReleased;
			std::bitset<Mouse::MaxMouseCode> ButtonsDown;
			std::bitset<Mouse::MaxMouseCode> ButtonsPressed;
			std::bitset<Mouse::MaxMouseCode> ButtonsReleased;
			glm::vec2 MousePosition{ 0.0f };
			glm::vec2 MouseDelta{ 0.0f };
			glm::vec2 ScrollDelta{ 0.0f };
			bool HasMousePosition = false;
		};

		InputState s_State;

		struct KeyName
		{
			KeyCode Code;
			const char* Name;
		};

		constexpr KeyName s_KeyNames[] = {
			{ Key::Space, "Space" }, { Key::Apostrophe, "Apostrophe" }, { Key::Comma, "Comma" }, { Key::Minus, "Minus" },
			{ Key::Period, "Period" }, { Key::Slash, "Slash" }, { Key::D0, "0" }, { Key::D1, "1" }, { Key::D2, "2" },
			{ Key::D3, "3" }, { Key::D4, "4" }, { Key::D5, "5" }, { Key::D6, "6" }, { Key::D7, "7" }, { Key::D8, "8" },
			{ Key::D9, "9" }, { Key::Semicolon, "Semicolon" }, { Key::Equal, "Equal" }, { Key::A, "A" }, { Key::B, "B" },
			{ Key::C, "C" }, { Key::D, "D" }, { Key::E, "E" }, { Key::F, "F" }, { Key::G, "G" }, { Key::H, "H" },
			{ Key::I, "I" }, { Key::J, "J" }, { Key::K, "K" }, { Key::L, "L" }, { Key::M, "M" }, { Key::N, "N" },
			{ Key::O, "O" }, { Key::P, "P" }, { Key::Q, "Q" }, { Key::R, "R" }, { Key::S, "S" }, { Key::T, "T" },
			{ Key::U, "U" }, { Key::V, "V" }, { Key::W, "W" }, { Key::X, "X" }, { Key::Y, "Y" }, { Key::Z, "Z" },
			{ Key::LeftBracket, "LeftBracket" }, { Key::Backslash, "Backslash" }, { Key::RightBracket, "RightBracket" },
			{ Key::GraveAccent, "GraveAccent" }, { Key::Escape, "Escape" }, { Key::Enter, "Enter" }, { Key::Tab, "Tab" },
			{ Key::Backspace, "Backspace" }, { Key::Insert, "Insert" }, { Key::Delete, "Delete" }, { Key::Right, "Right" },
			{ Key::Left, "Left" }, { Key::Down, "Down" }, { Key::Up, "Up" }, { Key::PageUp, "PageUp" },
			{ Key::PageDown, "PageDown" }, { Key::Home, "Home" }, { Key::End, "End" }, { Key::CapsLock, "CapsLock" },
			{ Key::ScrollLock, "ScrollLock" }, { Key::NumLock, "NumLock" }, { Key::PrintScreen, "PrintScreen" },
			{ Key::Pause, "Pause" }, { Key::F1, "F1" }, { Key::F2, "F2" }, { Key::F3, "F3" }, { Key::F4, "F4" },
			{ Key::F5, "F5" }, { Key::F6, "F6" }, { Key::F7, "F7" }, { Key::F8, "F8" }, { Key::F9, "F9" },
			{ Key::F10, "F10" }, { Key::F11, "F11" }, { Key::F12, "F12" }, { Key::KP0, "KP0" }, { Key::KP1, "KP1" },
			{ Key::KP2, "KP2" }, { Key::KP3, "KP3" }, { Key::KP4, "KP4" }, { Key::KP5, "KP5" }, { Key::KP6, "KP6" },
			{ Key::KP7, "KP7" }, { Key::KP8, "KP8" }, { Key::KP9, "KP9" }, { Key::KPEnter, "KPEnter" },
			{ Key::LeftShift, "LeftShift" }, { Key::LeftControl, "LeftControl" }, { Key::LeftAlt, "LeftAlt" },
			{ Key::LeftSuper, "LeftSuper" }, { Key::RightShift, "RightShift" }, { Key::RightControl, "RightControl" },
			{ Key::RightAlt, "RightAlt" }, { Key::RightSuper, "RightSuper" }, { Key::Menu, "Menu" }
		};

		bool IsValidKey(KeyCode key) { return key < Key::MaxKeyCode; }
		bool IsValidButton(MouseCode button) { return button < Mouse::MaxMouseCode; }

	}

	bool Input::IsKeyDown(KeyCode key) { return IsValidKey(key) && s_State.KeysDown.test(key); }
	bool Input::IsKeyPressed(KeyCode key) { return IsValidKey(key) && s_State.KeysPressed.test(key); }
	bool Input::IsKeyReleased(KeyCode key) { return IsValidKey(key) && s_State.KeysReleased.test(key); }

	bool Input::IsMouseButtonDown(MouseCode button) { return IsValidButton(button) && s_State.ButtonsDown.test(button); }
	bool Input::IsMouseButtonPressed(MouseCode button) { return IsValidButton(button) && s_State.ButtonsPressed.test(button); }
	bool Input::IsMouseButtonReleased(MouseCode button) { return IsValidButton(button) && s_State.ButtonsReleased.test(button); }

	glm::vec2 Input::GetMousePosition() { return s_State.MousePosition; }
	glm::vec2 Input::GetMouseDelta() { return s_State.MouseDelta; }
	glm::vec2 Input::GetScrollDelta() { return s_State.ScrollDelta; }

	void Input::OnKey(KeyCode key, bool down)
	{
		if (!IsValidKey(key))
			return;

		const bool wasDown = s_State.KeysDown.test(key);
		s_State.KeysDown.set(key, down);
		if (down && !wasDown)
			s_State.KeysPressed.set(key);
		else if (!down && wasDown)
			s_State.KeysReleased.set(key);
	}

	void Input::OnMouseButton(MouseCode button, bool down)
	{
		if (!IsValidButton(button))
			return;

		const bool wasDown = s_State.ButtonsDown.test(button);
		s_State.ButtonsDown.set(button, down);
		if (down && !wasDown)
			s_State.ButtonsPressed.set(button);
		else if (!down && wasDown)
			s_State.ButtonsReleased.set(button);
	}

	void Input::OnMouseMove(float x, float y)
	{
		const glm::vec2 position(x, y);
		if (s_State.HasMousePosition)
			s_State.MouseDelta += position - s_State.MousePosition;
		s_State.MousePosition = position;
		s_State.HasMousePosition = true;
	}

	void Input::OnScroll(float xOffset, float yOffset)
	{
		s_State.ScrollDelta += glm::vec2(xOffset, yOffset);
	}

	void Input::EndFrame()
	{
		s_State.KeysPressed.reset();
		s_State.KeysReleased.reset();
		s_State.ButtonsPressed.reset();
		s_State.ButtonsReleased.reset();
		s_State.MouseDelta = glm::vec2(0.0f);
		s_State.ScrollDelta = glm::vec2(0.0f);
	}

	void Input::Reset()
	{
		const glm::vec2 mousePosition = s_State.MousePosition;
		const bool hasMousePosition = s_State.HasMousePosition;
		s_State = InputState();
		s_State.MousePosition = mousePosition;
		s_State.HasMousePosition = hasMousePosition;
	}

	KeyCode Input::KeyFromName(std::string_view name)
	{
		for (const KeyName& entry : s_KeyNames)
		{
			const std::string_view entryName(entry.Name);
			if (entryName.size() != name.size())
				continue;

			bool equal = true;
			for (size_t i = 0; i < name.size() && equal; i++)
				equal = std::tolower(static_cast<unsigned char>(entryName[i])) == std::tolower(static_cast<unsigned char>(name[i]));
			if (equal)
				return entry.Code;
		}
		return 0;
	}

	const char* Input::KeyToName(KeyCode key)
	{
		for (const KeyName& entry : s_KeyNames)
		{
			if (entry.Code == key)
				return entry.Name;
		}
		return "Unknown";
	}

}
