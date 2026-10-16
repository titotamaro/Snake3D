#pragma once

#include "Ouroboros/Scripting/ScriptRuntime.h"

#include <vector>

struct ImDrawList;
struct ImFont;

namespace Ouroboros {

	// Draws script UI text (UI.Text) over a viewport rectangle.
	class GameHUD
	{
	public:
		static void Draw(ImDrawList* drawList, ImFont* font, const std::vector<UITextElement>& elements,
			float viewportX, float viewportY, float viewportWidth, float viewportHeight);
	};

}
