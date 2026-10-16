#pragma once

#include "Ouroboros/Scene/Entity.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Ouroboros {

	// Type-erased operations for every serializable component, keyed by name ("Transform", "MeshRenderer", ...).
	// Used by the scene serializer, the agent command API, the scripting API and the editor.
	struct ComponentInfo
	{
		std::string Name;
		bool Removable = true; // Tag and Transform are part of every entity and cannot be removed

		std::function<bool(Entity)> Has;
		std::function<void(Entity)> Add;      // adds a default-constructed component (no-op if present)
		std::function<void(Entity)> Remove;   // no-op if absent
		std::function<nlohmann::json(Entity)> Serialize;
		// Adds the component if needed, then patches it with the fields present in `data`. Throws nlohmann::json::exception.
		std::function<void(Entity, const nlohmann::json&)> Deserialize;
		std::function<void(Entity source, Entity destination)> Copy;
	};

	class ComponentRegistry
	{
	public:
		static const std::vector<ComponentInfo>& GetAll();
		static const ComponentInfo* Find(std::string_view name);
	};

}
