#pragma once

#include "Ouroboros/Asset/MeshData.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Ouroboros {

	// Imports glTF 2.0 (.gltf with external/embedded buffers, or binary .glb) into a single MeshData.
	// All triangle primitives of the default scene are flattened with their node transforms baked in; every
	// primitive becomes a submesh. Metallic-roughness materials and their textures are imported.
	class GltfImporter
	{
	public:
		static std::optional<MeshData> Import(const std::filesystem::path& path, std::string* error = nullptr);
	};

}
