#include "obpch.h"
#include "Ouroboros/ImGui/GameHUD.h"

#include <imgui.h>

namespace Ouroboros {

	void GameHUD::Draw(ImDrawList* drawList, ImFont* font, const std::vector<UITextElement>& elements,
		float viewportX, float viewportY, float viewportWidth, float viewportHeight)
	{
		if (!drawList || !font)
			return;

		for (const UITextElement& element : elements)
		{
			const float size = std::max(element.Size, 1.0f);
			const ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, element.Text.c_str());

			// Anchor point inside the viewport, and the alignment of the text box relative to it.
			const int anchor = static_cast<int>(element.Anchor);
			const float horizontal = static_cast<float>(anchor % 3) * 0.5f; // 0 = left, 0.5 = center, 1 = right
			const float vertical = static_cast<float>(anchor / 3) * 0.5f;   // 0 = top, 0.5 = middle, 1 = bottom

			const float anchorX = viewportX + viewportWidth * horizontal;
			const float anchorY = viewportY + viewportHeight * vertical;
			const ImVec2 position(anchorX + element.Position.x - textSize.x * horizontal, anchorY + element.Position.y - textSize.y * vertical);

			const ImU32 shadow = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.6f * element.Color.a));
			const ImU32 color = ImGui::GetColorU32(ImVec4(element.Color.r, element.Color.g, element.Color.b, element.Color.a));
			const float shadowOffset = std::max(1.0f, size * 0.05f);
			drawList->AddText(font, size, ImVec2(position.x + shadowOffset, position.y + shadowOffset), shadow, element.Text.c_str());
			drawList->AddText(font, size, position, color, element.Text.c_str());
		}
	}

}
