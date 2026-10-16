#include "ContentBrowserPanel.h"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <vector>

namespace Ouroboros {

	namespace {

		const char* FileKind(const std::filesystem::path& path)
		{
			const std::string extension = path.extension().string();
			if (extension == ".oscene")
				return "Scene";
			if (extension == ".gltf" || extension == ".glb")
				return "Mesh";
			if (extension == ".lua")
				return "Script";
			if (extension == ".hdr")
				return "Environment";
			if (extension == ".wav" || extension == ".mp3" || extension == ".ogg" || extension == ".flac")
				return "Audio";
			if (extension == ".png" || extension == ".jpg" || extension == ".jpeg")
				return "Texture";
			return "File";
		}

	}

	void ContentBrowserPanel::SetRoot(const std::filesystem::path& root)
	{
		m_Root = root;
		m_Current = root;
	}

	std::optional<std::filesystem::path> ContentBrowserPanel::OnImGuiRender()
	{
		std::optional<std::filesystem::path> opened;
		ImGui::Begin("Content Browser");

		if (m_Root.empty() || !std::filesystem::is_directory(m_Root))
		{
			ImGui::TextDisabled("No asset directory.");
			ImGui::End();
			return opened;
		}

		if (m_Current != m_Root)
		{
			if (ImGui::Button("<- Back"))
				m_Current = m_Current.parent_path();
			ImGui::SameLine();
		}
		ImGui::TextDisabled("%s", std::filesystem::relative(m_Current, m_Root.parent_path()).generic_string().c_str());
		ImGui::Separator();

		std::vector<std::filesystem::directory_entry> entries;
		std::error_code error;
		for (const auto& entry : std::filesystem::directory_iterator(m_Current, error))
		{
			if (!entry.path().filename().string().starts_with("."))
				entries.push_back(entry);
		}
		std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b)
		{
			if (a.is_directory() != b.is_directory())
				return a.is_directory();
			return a.path().filename() < b.path().filename();
		});

		if (ImGui::BeginTable("Assets", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			for (const auto& entry : entries)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				const std::string name = entry.path().filename().string();
				const bool isDirectory = entry.is_directory();
				ImGui::PushID(name.c_str());
				if (ImGui::Selectable(isDirectory ? ("[" + name + "]").c_str() : name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns))
				{
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						if (isDirectory)
							m_Current /= entry.path().filename();
						else
							opened = std::filesystem::relative(entry.path(), m_Root);
					}
				}
				if (!isDirectory && ImGui::BeginDragDropSource())
				{
					const std::string relative = std::filesystem::relative(entry.path(), m_Root).generic_string();
					ImGui::SetDragDropPayload("OB_ASSET_PATH", relative.c_str(), relative.size() + 1);
					ImGui::Text("%s", relative.c_str());
					ImGui::EndDragDropSource();
				}
				ImGui::PopID();
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", isDirectory ? "Folder" : FileKind(entry.path()));
			}
			ImGui::EndTable();
		}

		ImGui::End();
		return opened;
	}

}
