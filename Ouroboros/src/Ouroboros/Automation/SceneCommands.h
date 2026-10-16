#pragma once

#include "Ouroboros/Automation/CommandProcessor.h"
#include "Ouroboros/Core/Base.h"

#include <functional>

namespace Ouroboros {

	class Entity;
	class Scene;

	// Registers the engine-level automation commands that only need a scene:
	// entity.*, component.*, scene.info, scene.get, script.exec, input.*, log.*.
	// `getScene` returns the scene commands act on (the running scene in play mode, otherwise the edited scene).
	class SceneCommands
	{
	public:
		static void Register(CommandProcessor& processor, std::function<Ref<Scene>()> getScene);

		// Resolves {"entity": <id number | name string>} (or "id"/"name") to an entity, or throws CommandError.
		static Entity ResolveEntity(Scene& scene, const nlohmann::json& args);
		static nlohmann::json DescribeEntity(Entity entity);
	};

}
