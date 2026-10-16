#pragma once

#include "Ouroboros/Core/UUID.h"

#include <nlohmann/json.hpp>
#include <sol/sol.hpp>

namespace Ouroboros {

	class Entity;
	class Scene;
	class ScriptRuntime;

	// Script-side entity handle. It stores the UUID (not the ECS handle) and resolves it on every access, so scripts can
	// keep references to entities that are destroyed later without dangling.
	struct LuaEntity
	{
		Scene* SceneContext = nullptr;
		UUID ID = 0;

		Entity Resolve() const;
		// Like Resolve() but raises a Lua error if the entity no longer exists.
		Entity Get() const;
	};

	class ScriptGlue
	{
	public:
		// Registers the complete Ouroboros scripting API in the runtime's Lua state.
		static void Register(ScriptRuntime& runtime);

		// JSON <-> Lua conversion. Arrays of 2-4 numbers become vec2/vec3/vec4 and back; entities become their ID.
		static sol::object JsonToLua(sol::state_view lua, const nlohmann::json& value);
		// Tables nested deeper than MaxConversionDepth (e.g. self-referencing tables) raise an error.
		static nlohmann::json LuaToJson(const sol::object& value, int depth = 0);
		static constexpr int MaxConversionDepth = 32;
	};

}
