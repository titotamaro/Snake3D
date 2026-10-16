#pragma once

#include <Ouroboros/Core/UUID.h>
#include <Ouroboros/Scene/Scene.h>

namespace Ouroboros {

	// Lists the entities of a scene; selection, creation, duplication and deletion.
	class SceneHierarchyPanel
	{
	public:
		void OnImGuiRender(const Ref<Scene>& scene, UUID& selection);

		// Creates an entity preset by name ("Empty", "Cube", "Sphere", "Plane", "Camera", "Directional Light",
		// "Point Light", "Sky Light", "Audio Source"). Returns the new entity's UUID.
		static UUID CreatePreset(Scene& scene, const std::string& preset);
		static const std::vector<std::string>& GetPresets();
	};

}
