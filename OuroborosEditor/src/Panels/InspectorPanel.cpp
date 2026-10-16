#include "InspectorPanel.h"

#include <Ouroboros/Scene/ComponentRegistry.h>
#include <Ouroboros/Scene/ComponentSerialization.h>
#include <Ouroboros/Scene/Entity.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <glm/gtc/type_ptr.hpp>

namespace Ouroboros {

	namespace {

		void DrawVec3Control(const char* label, glm::vec3& values, float resetValue = 0.0f, float speed = 0.05f)
		{
			ImGui::PushID(label);
			ImGui::Columns(2, nullptr, false);
			ImGui::SetColumnWidth(0, 110.0f);
			ImGui::TextUnformatted(label);
			ImGui::NextColumn();

			ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
			const float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
			const ImVec2 buttonSize(lineHeight + 2.0f, lineHeight);

			const char* axes[3] = { "X", "Y", "Z" };
			const ImVec4 colors[3] = { ImVec4(0.75f, 0.15f, 0.15f, 1.0f), ImVec4(0.2f, 0.6f, 0.2f, 1.0f), ImVec4(0.15f, 0.3f, 0.8f, 1.0f) };
			for (int axis = 0; axis < 3; axis++)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, colors[axis]);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors[axis]);
				if (ImGui::Button(axes[axis], buttonSize))
					values[axis] = resetValue;
				ImGui::PopStyleColor(2);
				ImGui::SameLine();
				ImGui::PushID(axis);
				ImGui::DragFloat("##value", &values[axis], speed, 0.0f, 0.0f, "%.3f");
				ImGui::PopID();
				ImGui::PopItemWidth();
				if (axis < 2)
					ImGui::SameLine();
			}

			ImGui::PopStyleVar();
			ImGui::Columns(1);
			ImGui::PopID();
		}

		template<typename T, typename DrawFunction>
		void DrawComponent(const char* name, Entity entity, bool removable, DrawFunction draw)
		{
			T* component = entity.TryGetComponent<T>();
			if (!component)
				return;

			constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth |
				ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding;

			ImGui::PushID(name);
			const float contentWidth = ImGui::GetContentRegionAvail().x;
			const bool open = ImGui::TreeNodeEx("##component", flags, "%s", name);
			bool remove = false;
			if (removable)
			{
				ImGui::SameLine(contentWidth - 22.0f);
				if (ImGui::SmallButton("x"))
					remove = true;
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Remove component");
			}
			if (open)
			{
				draw(*component);
				ImGui::TreePop();
			}
			ImGui::PopID();

			if (remove)
				entity.RemoveComponent<T>();
		}

		bool InputPath(const char* label, std::string& path)
		{
			bool changed = ImGui::InputText(label, &path);
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OB_ASSET_PATH"))
				{
					path = std::string(static_cast<const char*>(payload->Data));
					changed = true;
				}
				ImGui::EndDragDropTarget();
			}
			return changed;
		}

		bool FloatOrDefault(const char* label, float& value, float minimum, float maximum, const char* tooltip)
		{
			bool overridden = value >= 0.0f;
			bool changed = false;
			ImGui::PushID(label);
			if (ImGui::Checkbox("##override", &overridden))
			{
				value = overridden ? 0.5f : -1.0f;
				changed = true;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", tooltip);
			ImGui::SameLine();
			ImGui::BeginDisabled(!overridden);
			float shown = overridden ? value : 0.0f;
			if (ImGui::SliderFloat(label, &shown, minimum, maximum) && overridden)
			{
				value = shown;
				changed = true;
			}
			ImGui::EndDisabled();
			ImGui::PopID();
			return changed;
		}

	}

	void InspectorPanel::OnImGuiRender(const Ref<Scene>& scene, UUID selection)
	{
		ImGui::Begin("Inspector");
		Entity entity = scene ? scene->GetEntityByUUID(selection) : Entity();
		if (entity)
			DrawComponents(entity);
		else
			ImGui::TextDisabled("Select an entity to edit it.");
		ImGui::End();
	}

	void InspectorPanel::DrawComponents(Entity entity)
	{
		std::string& tag = entity.GetComponent<TagComponent>().Tag;
		ImGui::SetNextItemWidth(-90.0f);
		ImGui::InputText("##Tag", &tag);
		ImGui::SameLine();
		if (ImGui::Button("Add..."))
			ImGui::OpenPopup("AddComponent");
		if (ImGui::BeginPopup("AddComponent"))
		{
			for (const ComponentInfo& info : ComponentRegistry::GetAll())
			{
				if (!info.Removable || info.Has(entity))
					continue;
				if (ImGui::MenuItem(info.Name.c_str()))
					info.Add(entity);
			}
			ImGui::EndPopup();
		}
		ImGui::TextDisabled("ID %llu", static_cast<unsigned long long>(static_cast<uint64_t>(entity.GetUUID())));
		ImGui::Separator();

		DrawComponent<TransformComponent>("Transform", entity, false, [](TransformComponent& component)
		{
			DrawVec3Control("Translation", component.Translation);
			glm::vec3 rotation = glm::degrees(component.Rotation);
			DrawVec3Control("Rotation", rotation, 0.0f, 0.5f);
			component.Rotation = glm::radians(rotation);
			DrawVec3Control("Scale", component.Scale, 1.0f);
		});

		DrawComponent<CameraComponent>("Camera", entity, true, [](CameraComponent& component)
		{
			const char* projections[] = { "Perspective", "Orthographic" };
			int projection = static_cast<int>(component.Projection);
			if (ImGui::Combo("Projection", &projection, projections, 2))
				component.Projection = static_cast<ProjectionType>(projection);
			ImGui::Checkbox("Primary", &component.Primary);
			if (component.Projection == ProjectionType::Perspective)
			{
				float fov = glm::degrees(component.PerspectiveFOV);
				if (ImGui::SliderFloat("Field of View", &fov, 10.0f, 120.0f))
					component.PerspectiveFOV = glm::radians(fov);
				ImGui::DragFloat("Near", &component.PerspectiveNear, 0.01f, 0.001f, 100.0f);
				ImGui::DragFloat("Far", &component.PerspectiveFar, 1.0f, 1.0f, 10000.0f);
			}
			else
			{
				ImGui::DragFloat("Size", &component.OrthographicSize, 0.1f, 0.1f, 1000.0f);
				ImGui::DragFloat("Near", &component.OrthographicNear, 0.1f);
				ImGui::DragFloat("Far", &component.OrthographicFar, 0.1f);
			}
			ImGui::DragFloat("Exposure", &component.Exposure, 0.01f, 0.0f, 16.0f);
		});

		DrawComponent<MeshRendererComponent>("Mesh Renderer", entity, true, [](MeshRendererComponent& component)
		{
			InputPath("Mesh", component.Mesh);
			if (ImGui::BeginCombo("Primitive", "Choose..."))
			{
				for (const char* primitive : { "Primitive:Cube", "Primitive:Sphere", "Primitive:Plane" })
				{
					if (ImGui::Selectable(primitive))
						component.Mesh = primitive;
				}
				ImGui::EndCombo();
			}
			ImGui::ColorEdit4("Color", glm::value_ptr(component.Color));
			FloatOrDefault("Metallic", component.Metallic, 0.0f, 1.0f, "Override the material's metallic value");
			FloatOrDefault("Roughness", component.Roughness, 0.0f, 1.0f, "Override the material's roughness value");
			ImGui::ColorEdit3("Emissive", glm::value_ptr(component.Emissive));
			ImGui::DragFloat("Emissive Intensity", &component.EmissiveIntensity, 0.05f, 0.0f, 100.0f);
			ImGui::Checkbox("Cast Shadows", &component.CastShadows);
			ImGui::Checkbox("Visible", &component.Visible);
		});

		DrawComponent<DirectionalLightComponent>("Directional Light", entity, true, [](DirectionalLightComponent& component)
		{
			ImGui::ColorEdit3("Color", glm::value_ptr(component.Color));
			ImGui::DragFloat("Intensity", &component.Intensity, 0.05f, 0.0f, 100.0f);
			ImGui::Checkbox("Cast Shadows", &component.CastShadows);
			ImGui::SliderFloat("Shadow Softness", &component.ShadowSoftness, 0.0f, 4.0f);
			ImGui::DragFloat("Shadow Distance", &component.ShadowDistance, 0.5f, 1.0f, 1000.0f);
		});

		DrawComponent<PointLightComponent>("Point Light", entity, true, [](PointLightComponent& component)
		{
			ImGui::ColorEdit3("Color", glm::value_ptr(component.Color));
			ImGui::DragFloat("Intensity", &component.Intensity, 0.1f, 0.0f, 1000.0f);
			ImGui::DragFloat("Radius", &component.Radius, 0.1f, 0.01f, 1000.0f);
		});

		DrawComponent<SkyLightComponent>("Sky Light", entity, true, [](SkyLightComponent& component)
		{
			InputPath("Environment (.hdr)", component.Environment);
			ImGui::DragFloat("Intensity", &component.Intensity, 0.01f, 0.0f, 16.0f);
			float rotation = glm::degrees(component.Rotation);
			if (ImGui::SliderFloat("Rotation", &rotation, -180.0f, 180.0f))
				component.Rotation = glm::radians(rotation);
			ImGui::Checkbox("Show Background", &component.ShowBackground);
		});

		DrawComponent<RigidBodyComponent>("Rigid Body", entity, true, [](RigidBodyComponent& component)
		{
			const char* types[] = { "Static", "Dynamic", "Kinematic" };
			int type = static_cast<int>(component.Type);
			if (ImGui::Combo("Type", &type, types, 3))
				component.Type = static_cast<RigidBodyType>(type);
			ImGui::DragFloat("Mass", &component.Mass, 0.05f, 0.001f, 10000.0f);
			ImGui::DragFloat("Linear Damping", &component.LinearDamping, 0.01f, 0.0f, 10.0f);
			ImGui::DragFloat("Angular Damping", &component.AngularDamping, 0.01f, 0.0f, 10.0f);
			ImGui::DragFloat("Gravity Factor", &component.GravityFactor, 0.01f, -10.0f, 10.0f);
			ImGui::Checkbox("Fixed Rotation", &component.FixedRotation);
		});

		DrawComponent<BoxColliderComponent>("Box Collider", entity, true, [](BoxColliderComponent& component)
		{
			DrawVec3Control("Half Extents", component.HalfExtents, 0.5f);
			DrawVec3Control("Offset", component.Offset);
			ImGui::DragFloat("Friction", &component.Friction, 0.01f, 0.0f, 2.0f);
			ImGui::DragFloat("Restitution", &component.Restitution, 0.01f, 0.0f, 1.0f);
			ImGui::Checkbox("Is Trigger", &component.IsTrigger);
		});

		DrawComponent<SphereColliderComponent>("Sphere Collider", entity, true, [](SphereColliderComponent& component)
		{
			ImGui::DragFloat("Radius", &component.Radius, 0.01f, 0.001f, 1000.0f);
			DrawVec3Control("Offset", component.Offset);
			ImGui::DragFloat("Friction", &component.Friction, 0.01f, 0.0f, 2.0f);
			ImGui::DragFloat("Restitution", &component.Restitution, 0.01f, 0.0f, 1.0f);
			ImGui::Checkbox("Is Trigger", &component.IsTrigger);
		});

		DrawComponent<ScriptComponent>("Script", entity, true, [this, entity](ScriptComponent& component)
		{
			InputPath("Script (.lua)", component.Script);
			DrawScriptFields(entity);
		});

		DrawComponent<AudioSourceComponent>("Audio Source", entity, true, [](AudioSourceComponent& component)
		{
			InputPath("Clip", component.Clip);
			ImGui::SliderFloat("Volume", &component.Volume, 0.0f, 2.0f);
			ImGui::SliderFloat("Pitch", &component.Pitch, 0.1f, 4.0f);
			ImGui::Checkbox("Loop", &component.Loop);
			ImGui::Checkbox("Play On Start", &component.PlayOnStart);
			ImGui::Checkbox("Spatial", &component.Spatial);
		});
	}

	void InspectorPanel::DrawScriptFields(Entity entity)
	{
		ScriptComponent& component = entity.GetComponent<ScriptComponent>();
		ImGui::TextDisabled("Field overrides (applied over the script's Properties)");

		std::string toRemove;
		for (auto it = component.Fields.begin(); it != component.Fields.end(); ++it)
		{
			nlohmann::json& value = it.value();
			ImGui::PushID(it.key().c_str());
			if (ImGui::SmallButton("-"))
				toRemove = it.key();
			ImGui::SameLine();
			const std::string label = it.key();
			if (value.is_boolean())
			{
				bool flag = value.get<bool>();
				if (ImGui::Checkbox(label.c_str(), &flag))
					value = flag;
			}
			else if (value.is_number())
			{
				float number = value.get<float>();
				if (ImGui::DragFloat(label.c_str(), &number, 0.05f))
					value = number;
			}
			else if (value.is_string())
			{
				std::string text = value.get<std::string>();
				if (ImGui::InputText(label.c_str(), &text))
					value = text;
			}
			else if (value.is_array() && value.size() == 3 && value[0].is_number())
			{
				glm::vec3 vector = value.get<glm::vec3>();
				if (ImGui::DragFloat3(label.c_str(), glm::value_ptr(vector), 0.05f))
					value = vector;
			}
			else
			{
				ImGui::Text("%s = %s", label.c_str(), value.dump().c_str());
			}
			ImGui::PopID();
		}
		if (!toRemove.empty())
			component.Fields.erase(toRemove);

		ImGui::SetNextItemWidth(120.0f);
		ImGui::InputText("##newField", m_NewFieldName, sizeof(m_NewFieldName));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90.0f);
		const char* types[] = { "Number", "Bool", "String", "Vec3" };
		ImGui::Combo("##newFieldType", &m_NewFieldType, types, 4);
		ImGui::SameLine();
		if (ImGui::Button("Add Field") && m_NewFieldName[0] != '\0')
		{
			nlohmann::json initial = 0.0;
			if (m_NewFieldType == 1)
				initial = false;
			else if (m_NewFieldType == 2)
				initial = "";
			else if (m_NewFieldType == 3)
				initial = glm::vec3(0.0f);
			component.Fields[m_NewFieldName] = initial;
			m_NewFieldName[0] = '\0';
		}
	}

}
