#pragma once

#include "Ouroboros/Asset/MeshData.h"

#include <optional>
#include <string_view>

namespace Ouroboros {

	// Built-in primitive meshes, addressed by MeshRenderer paths such as "Primitive:Cube".
	class MeshFactory
	{
	public:
		static constexpr std::string_view PrimitivePrefix = "Primitive:";

		static MeshData CreateCube();                             // 1 x 1 x 1, centered
		static MeshData CreateSphere(uint32_t segments = 32, uint32_t rings = 16); // radius 0.5
		static MeshData CreatePlane();                            // 1 x 1 on XZ, facing +Y

		static bool IsPrimitivePath(std::string_view path);
		// Returns the primitive for "Primitive:<Name>", or nullopt for an unknown name.
		static std::optional<MeshData> CreatePrimitive(std::string_view path);
	};

}
