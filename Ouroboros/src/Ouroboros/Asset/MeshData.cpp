#include "obpch.h"
#include "Ouroboros/Asset/MeshData.h"

namespace Ouroboros {

	void MeshData::ComputeBounds()
	{
		if (Vertices.empty())
		{
			BoundsMin = BoundsMax = glm::vec3(0.0f);
			return;
		}

		BoundsMin = glm::vec3(std::numeric_limits<float>::max());
		BoundsMax = glm::vec3(std::numeric_limits<float>::lowest());
		for (const Vertex& vertex : Vertices)
		{
			BoundsMin = glm::min(BoundsMin, vertex.Position);
			BoundsMax = glm::max(BoundsMax, vertex.Position);
		}
	}

	void MeshData::GenerateTangents()
	{
		std::vector<glm::vec3> tangents(Vertices.size(), glm::vec3(0.0f));
		std::vector<glm::vec3> bitangents(Vertices.size(), glm::vec3(0.0f));

		for (size_t i = 0; i + 2 < Indices.size(); i += 3)
		{
			const uint32_t i0 = Indices[i], i1 = Indices[i + 1], i2 = Indices[i + 2];
			const Vertex& v0 = Vertices[i0];
			const Vertex& v1 = Vertices[i1];
			const Vertex& v2 = Vertices[i2];

			const glm::vec3 edge1 = v1.Position - v0.Position;
			const glm::vec3 edge2 = v2.Position - v0.Position;
			const glm::vec2 deltaUV1 = v1.TexCoord - v0.TexCoord;
			const glm::vec2 deltaUV2 = v2.TexCoord - v0.TexCoord;

			const float determinant = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
			if (std::abs(determinant) < 1e-12f)
				continue;
			const float inverse = 1.0f / determinant;

			const glm::vec3 tangent = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * inverse;
			const glm::vec3 bitangent = (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * inverse;
			for (uint32_t index : { i0, i1, i2 })
			{
				tangents[index] += tangent;
				bitangents[index] += bitangent;
			}
		}

		for (size_t i = 0; i < Vertices.size(); i++)
		{
			const glm::vec3& normal = Vertices[i].Normal;
			glm::vec3 tangent = tangents[i] - normal * glm::dot(normal, tangents[i]);
			if (glm::dot(tangent, tangent) < 1e-12f)
			{
				// Degenerate UVs: pick any vector perpendicular to the normal.
				const glm::vec3 axis = std::abs(normal.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
				tangent = glm::cross(normal, axis);
			}
			tangent = glm::normalize(tangent);
			const float handedness = glm::dot(glm::cross(normal, tangent), bitangents[i]) < 0.0f ? -1.0f : 1.0f;
			Vertices[i].Tangent = glm::vec4(tangent, handedness);
		}
	}

	bool MeshData::Validate(std::string* error) const
	{
		auto fail = [error](const std::string& message)
		{
			if (error)
				*error = message;
			return false;
		};

		if (Vertices.empty() || Indices.empty())
			return fail("mesh has no geometry");
		if (Indices.size() % 3 != 0)
			return fail("index count is not a multiple of 3");
		for (uint32_t index : Indices)
		{
			if (index >= Vertices.size())
				return fail("index out of range");
		}
		if (Submeshes.empty())
			return fail("mesh has no submeshes");
		for (const Submesh& submesh : Submeshes)
		{
			if (static_cast<uint64_t>(submesh.IndexOffset) + submesh.IndexCount > Indices.size())
				return fail("submesh index range out of bounds");
			if (submesh.MaterialIndex >= Materials.size())
				return fail("submesh material index out of range");
		}
		return true;
	}

}
