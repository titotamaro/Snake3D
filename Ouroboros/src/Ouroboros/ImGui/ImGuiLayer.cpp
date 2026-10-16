#include "obpch.h"
#include "Ouroboros/ImGui/ImGuiLayer.h"

#include "Ouroboros/Core/FileSystem.h"
#include "Ouroboros/ImGui/ImGuiRenderer.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <ImGuizmo.h>

namespace Ouroboros {

	ImGuiLayer::ImGuiLayer(GLFWwindow* window, Renderer& renderer, nvrhi::Format targetFormat, const ImGuiLayerSpecification& specification)
		: m_IniFile(specification.IniFile)
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		if (specification.EnableDocking)
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.IniFilename = m_IniFile.empty() ? nullptr : m_IniFile.c_str();

		const std::filesystem::path fontPath = FileSystem::GetEngineResourcesDirectory() / "Fonts" / "Roboto-Medium.ttf";
		const std::string font = fontPath.string();
		if (std::filesystem::exists(fontPath))
		{
			m_DefaultFont = io.Fonts->AddFontFromFileTTF(font.c_str(), 16.0f);
			m_BoldFont = io.Fonts->AddFontFromFileTTF(font.c_str(), 18.0f);
			m_HUDFont = io.Fonts->AddFontFromFileTTF(font.c_str(), HUDFontSize);
		}
		else
		{
			OB_CORE_WARN("Font '{}' not found, using the built-in ImGui font", font);
			m_DefaultFont = io.Fonts->AddFontDefault();
			m_BoldFont = m_DefaultFont;
			m_HUDFont = m_DefaultFont;
		}
		io.FontDefault = m_DefaultFont;

		SetDarkTheme();

		ImGui_ImplGlfw_InitForVulkan(window, true);
		io.BackendRendererName = "Ouroboros NVRHI";

		m_Renderer = CreateScope<ImGuiRenderer>(renderer, targetFormat);
		m_Renderer->CreateFontTexture();
	}

	ImGuiLayer::~ImGuiLayer()
	{
		m_Renderer.reset();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

	void ImGuiLayer::Begin()
	{
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
	}

	void ImGuiLayer::End(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer)
	{
		ImGui::Render();
		if (framebuffer)
			m_Renderer->Render(commandList, framebuffer, ImGui::GetDrawData());
	}

	bool ImGuiLayer::WantsMouse() const
	{
		return ImGui::GetIO().WantCaptureMouse;
	}

	bool ImGuiLayer::WantsKeyboard() const
	{
		return ImGui::GetIO().WantCaptureKeyboard;
	}

	void ImGuiLayer::SetDarkTheme()
	{
		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 4.0f;
		style.FrameRounding = 3.0f;
		style.GrabRounding = 3.0f;
		style.TabRounding = 3.0f;
		style.WindowBorderSize = 1.0f;
		style.FramePadding = ImVec2(6.0f, 4.0f);

		ImVec4* colors = style.Colors;
		colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.11f, 0.12f, 1.0f);
		colors[ImGuiCol_Header] = ImVec4(0.20f, 0.21f, 0.23f, 1.0f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.31f, 0.33f, 1.0f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.16f, 0.16f, 0.17f, 1.0f);
		colors[ImGuiCol_Button] = ImVec4(0.20f, 0.21f, 0.23f, 1.0f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.31f, 0.33f, 1.0f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.16f, 0.17f, 1.0f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.20f, 1.0f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.27f, 0.29f, 1.0f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.14f, 0.14f, 0.15f, 1.0f);
		colors[ImGuiCol_Tab] = ImVec4(0.14f, 0.14f, 0.15f, 1.0f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.36f, 0.37f, 0.39f, 1.0f);
		colors[ImGuiCol_TabSelected] = ImVec4(0.24f, 0.25f, 0.27f, 1.0f);
		colors[ImGuiCol_TabDimmed] = ImVec4(0.14f, 0.14f, 0.15f, 1.0f);
		colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.20f, 0.21f, 0.23f, 1.0f);
		colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.12f, 0.13f, 1.0f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.14f, 0.15f, 1.0f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.36f, 0.78f, 0.42f, 1.0f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.36f, 0.78f, 0.42f, 1.0f);
		colors[ImGuiCol_DockingPreview] = ImVec4(0.36f, 0.78f, 0.42f, 0.7f);
	}

}
