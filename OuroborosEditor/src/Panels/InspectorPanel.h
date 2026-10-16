#pragma once

#include <Ouroboros/Core/UUID.h>
#include <Ouroboros/Scene/Scene.h>

namespace Ouroboros {

	class Entity;

	// Shows and edits every component of the selected entity.
	class InspectorPanel
	{
	public:
		void OnImGuiRender(const Ref<Scene>& scene, UUID selection);

	private:
		void DrawComponents(Entity entity);
		void DrawScriptFields(Entity entity);

	private:
		char m_NewFieldName[64] = {};
		int m_NewFieldType = 0;
	};

}
