#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Ouroboros {

	// Interleaved vertex format used by every mesh (48 bytes). Matches VertexInput in Common.hlsli.
	struct Vertex
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		glm::vec4 Tangent{ 1.0f, 0.0f, 0.0f, 1.0f }; // xyz = tangent, w = bitangent sign
		glm::vec2 TexCoord{ 0.0f };
	};
	static_assert(sizeof(Vertex) == 48, "Vertex layout must match the shaders");

	struct Submesh
	{
		uint32_t IndexOffset = 0;
		uint32_t IndexCount = 0;
		uint32_t MaterialIndex = 0;
	};

	// A texture referenced by a material: either a file on disk or encoded image bytes embedded in a model (.glb).
	struct TextureSource
	{
		std::filesystem::path Path;
		std::vector<uint8_t> EncodedData;
		std::string Name; // identifies embedded images in caches

		bool IsValid() const { return !Path.empty() || !EncodedData.empty(); }
		std::string GetCacheKey() const { return EncodedData.empty() ? Path.generic_string() : Name; }
	};

	// glTF-style metallic-roughness material description (CPU side).
	struct MaterialData
	{
		std::string Name = "Default";
		glm::vec4 BaseColorFactor{ 1.0f };
		glm::vec3 EmissiveFactor{ 0.0f };
		float EmissiveStrength = 1.0f;
		float MetallicFactor = 0.0f;
		float RoughnessFactor = 0.5f;
		float NormalScale = 1.0f;
		float OcclusionStrength = 1.0f;

		TextureSource BaseColorTexture;         // sRGB
		TextureSource MetallicRoughnessTexture; // linear: G = roughness, B = metallic
		TextureSource NormalTexture;            // linear, tangent space
		TextureSource OcclusionTexture;         // linear: R = occlusion
		TextureSource EmissiveTexture;          // sRGB
	};

	struct MeshData
	{
		std::vector<Vertex> Vertices;
		std::vector<uint32_t> Indices;
		std::vector<Submesh> Submeshes;
		std::vector<MaterialData> Materials;
		glm::vec3 BoundsMin{ 0.0f };
		glm::vec3 BoundsMax{ 0.0f };

		void ComputeBounds();
		// Computes per-vertex tangents from UVs (used when a model provides none).
		void GenerateTangents();
		// Checks index ranges, submesh ranges and material indices. Returns false and fills `error` if invalid.
		bool Validate(std::string* error = nullptr) const;
	};

}
