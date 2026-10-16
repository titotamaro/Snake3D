#include "ConsolePanel.h"

#include <Ouroboros/Core/Log.h>

#include <imgui.h>

#include <cstring>

namespace Ouroboros {

	void ConsolePanel::OnImGuiRender()
	{
		ImGui::Begin("Console");

		if (ImGui::Button("Clear"))
			Log::GetBuffer().Clear();
		ImGui::SameLine();
		ImGui::Checkbox("Trace", &m_ShowTrace);
		ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &m_AutoScroll);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##filter", "Filter", m_Filter, sizeof(m_Filter));
		ImGui::Separator();

		ImGui::BeginChild("Messages", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
		for (const LogMessage& message : Log::GetBuffer().GetMessages())
		{
			if (!m_ShowTrace && message.Level == LogLevel::Trace)
				continue;
			if (m_Filter[0] != '\0' && message.Text.find(m_Filter) == std::string::npos)
				continue;

			ImVec4 color(0.85f, 0.85f, 0.85f, 1.0f);
			switch (message.Level)
			{
				case LogLevel::Trace:
					color = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
					break;
				case LogLevel::Warn:
					color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
					break;
				case LogLevel::Error:
				case LogLevel::Critical:
					color = ImVec4(1.0f, 0.4f, 0.35f, 1.0f);
					break;
				default:
					break;
			}
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			const std::string line = (message.Logger == "APP" ? "[Game] " : "") + message.Text;
			ImGui::TextUnformatted(line.c_str());
			ImGui::PopStyleColor();
		}
		if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f)
			ImGui::SetScrollHereY(1.0f);
		ImGui::EndChild();

		ImGui::End();
	}

}
