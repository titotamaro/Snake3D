#pragma once

#include "Ouroboros/Core/Base.h"

#include <nvrhi/nvrhi.h>

#include <string>

struct GLFWwindow;
struct ImFont;

namespace Ouroboros {

	class ImGuiRenderer;
	class Renderer;

	struct ImGuiLayerSpecification
	{
		bool EnableDocking = false;
		std::string IniFile; // empty disables imgui.ini persistence
	};

	// Owns the Dear ImGui context, the GLFW platform backend and the NVRHI renderer backend.
	class ImGuiLayer
	{
	public:
		ImGuiLayer(GLFWwindow* window, Renderer& renderer, nvrhi::Format targetFormat, const ImGuiLayerSpecification& specification);
		~ImGuiLayer();

		void Begin();
		void End(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer);

		bool WantsMouse() const;
		bool WantsKeyboard() const;

		ImFont* GetDefaultFont() const { return m_DefaultFont; }
		ImFont* GetBoldFont() const { return m_BoldFont; }
		// Large font used for the in-game HUD (scaled down per text size).
		ImFont* GetHUDFont() const { return m_HUDFont; }

		static constexpr float HUDFontSize = 64.0f;

	private:
		void SetDarkTheme();

	private:
		Scope<ImGuiRenderer> m_Renderer;
		std::string m_IniFile;
		ImFont* m_DefaultFont = nullptr;
		ImFont* m_BoldFont = nullptr;
		ImFont* m_HUDFont = nullptr;
	};

}
