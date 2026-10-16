#pragma once

#include "Ouroboros/Core/KeyCodes.h"

#include <glm/glm.hpp>

#include <string_view>

namespace Ouroboros {

	// Global input state. The window feeds it through the On* functions; tests and agents may inject input the same way.
	// "Pressed"/"Released" queries are edge-triggered and valid for the frame in which the transition happened.
	class Input
	{
	public:
		static bool IsKeyDown(KeyCode key);
		static bool IsKeyPressed(KeyCode key);
		static bool IsKeyReleased(KeyCode key);

		static bool IsMouseButtonDown(MouseCode button);
		static bool IsMouseButtonPressed(MouseCode button);
		static bool IsMouseButtonReleased(MouseCode button);

		static glm::vec2 GetMousePosition();
		static glm::vec2 GetMouseDelta();
		static glm::vec2 GetScrollDelta();

		static void OnKey(KeyCode key, bool down);
		static void OnMouseButton(MouseCode button, bool down);
		static void OnMouseMove(float x, float y);
		static void OnScroll(float xOffset, float yOffset);

		// Clears edge-triggered state. Called by the application at the end of every frame.
		static void EndFrame();
		// Releases every key and button (e.g. when focus is lost or play mode stops).
		static void Reset();

		// Converts between key names ("W", "Space", "Left", ...) and key codes. Unknown names return 0.
		static KeyCode KeyFromName(std::string_view name);
		static const char* KeyToName(KeyCode key);
	};

}
