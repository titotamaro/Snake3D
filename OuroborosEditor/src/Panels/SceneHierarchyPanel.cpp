#include "SceneHierarchyPanel.h"

#include <Ouroboros/Scene/Entity.h>

#include <imgui.h>

namespace Ouroboros {

	const std::vector<std::string>& SceneHierarchyPanel::GetPresets()
	{
		static const std::vector<std::string> s_Presets = {
			"Empty", "Cube", "Sphere", "Plane", "Camera", "Directional Light", "Point Light", "Sky Light", "Audio Source"
		};
		return s_Presets;
	}

	UUID SceneHierarchyPanel::CreatePreset(Scene& scene, const std::string& preset)
	{
		Entity entity = scene.CreateEntity(preset == "Empty" ? std::string("Entity") : preset);
		if (preset == "Cube" || preset == "Sphere" || preset == "Plane")
			entity.AddComponent<MeshRendererComponent>().Mesh = "Primitive:" + preset;
		else if (preset == "Camera")
			entity.AddComponent<CameraComponent>().Primary = scene.GetPrimaryCameraEntity() ? false : true;
		else if (preset == "Directional Light")
		{
			entity.AddComponent<DirectionalLightComponent>();
			entity.GetTransform().Rotation = glm::vec3(-0.9f, 0.5f, 0.0f);
		}
		else if (preset == "Point Light")
			entity.AddComponent<PointLightComponent>();
		else if (preset == "Sky Light")
			entity.AddComponent<SkyLightComponent>();
		else if (preset == "Audio Source")
			entity.AddComponent<AudioSourceComponent>();
		return entity.GetUUID();
	}

	void SceneHierarchyPanel::OnImGuiRender(const Ref<Scene>& scene, UUID& selection)
	{
		ImGui::Begin("Hierarchy");
		if (!scene)
		{
			ImGui::End();
			return;
		}

		UUID toDelete = 0;
		UUID toDuplicate = 0;
		for (UUID uuid : scene->GetEntityOrder())
		{
			Entity entity = scene->GetEntityByUUID(uuid);
			ImGui::PushID(static_cast<int>(static_cast<uint64_t>(uuid) & 0x7FFFFFFF));
			const bool selected = selection == uuid;
			if (ImGui::Selectable(entity.GetName().c_str(), selected))
				selection = uuid;
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Duplicate"))
					toDuplicate = uuid;
				if (ImGui::MenuItem("Delete"))
					toDelete = uuid;
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}

		if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
			selection = 0;

		if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			if (ImGui::BeginMenu("Create"))
			{
				for (const std::string& preset : GetPresets())
				{
					if (ImGui::MenuItem(preset.c_str()))
						selection = CreatePreset(*scene, preset);
				}
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}

		if (toDuplicate)
		{
			Entity duplicate = scene->DuplicateEntity(scene->GetEntityByUUID(toDuplicate));
			selection = duplicate.GetUUID();
		}
		if (toDelete)
		{
			scene->DestroyEntity(scene->GetEntityByUUID(toDelete));
			if (selection == toDelete)
				selection = 0;
		}

		ImGui::End();
	}

}
