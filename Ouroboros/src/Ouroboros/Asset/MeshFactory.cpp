#include "obpch.h"
#include "Ouroboros/Asset/MeshFactory.h"

#include "Ouroboros/Math/Math.h"

namespace Ouroboros {

	namespace {

		void FinalizePrimitive(MeshData& mesh, const char* name)
		{
			MaterialData material;
			material.Name = name;
			mesh.Materials = { material };
			mesh.Submeshes = { Submesh{ 0, static_cast<uint32_t>(mesh.Indices.size()), 0 } };
			mesh.GenerateTangents();
			mesh.ComputeBounds();
		}

	}

	MeshData MeshFactory::CreateCube()
	{
		MeshData mesh;

		struct Face
		{
			glm::vec3 Normal, Up, Right;
		};
		const Face faces[6] = {
			{ { 0, 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 } },
			{ { 0, 0, -1 }, { 0, 1, 0 }, { -1, 0, 0 } },
			{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, -1 } },
			{ { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
			{ { 0, 1, 0 }, { 0, 0, -1 }, { 1, 0, 0 } },
			{ { 0, -1, 0 }, { 0, 0, 1 }, { 1, 0, 0 } }
		};

		for (const Face& face : faces)
		{
			const uint32_t base = static_cast<uint32_t>(mesh.Vertices.size());
			const glm::vec2 corners[4] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
			for (const glm::vec2& corner : corners)
			{
				Vertex vertex;
				vertex.Position = 0.5f * (face.Normal + face.Right * corner.x + face.Up * corner.y);
				vertex.Normal = face.Normal;
				vertex.TexCoord = glm::vec2(corner.x * 0.5f + 0.5f, 0.5f - corner.y * 0.5f);
				mesh.Vertices.push_back(vertex);
			}
			// Counter-clockwise when viewed from outside.
			mesh.Indices.insert(mesh.Indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
		}

		FinalizePrimitive(mesh, "Cube");
		return mesh;
	}

	MeshData MeshFactory::CreateSphere(uint32_t segments, uint32_t rings)
	{
		segments = std::max(segments, 3u);
		rings = std::max(rings, 2u);

		MeshData mesh;
		for (uint32_t ring = 0; ring <= rings; ring++)
		{
			const float v = static_cast<float>(ring) / static_cast<float>(rings);
			const float theta = v * Math::Pi;
			for (uint32_t segment = 0; segment <= segments; segment++)
			{
				const float u = static_cast<float>(segment) / static_cast<float>(segments);
				const float phi = u * 2.0f * Math::Pi;
				const glm::vec3 normal(std::sin(theta) * std::cos(phi), std::cos(theta), -std::sin(theta) * std::sin(phi));

				Vertex vertex;
				vertex.Position = normal * 0.5f;
				vertex.Normal = normal;
				vertex.TexCoord = glm::vec2(u, v);
				mesh.Vertices.push_back(vertex);
			}
		}

		const uint32_t stride = segments + 1;
		for (uint32_t ring = 0; ring < rings; ring++)
		{
			for (uint32_t segment = 0; segment < segments; segment++)
			{
				const uint32_t a = ring * stride + segment;
				const uint32_t b = a + stride;
				mesh.Indices.insert(mesh.Indices.end(), { a, b, a + 1, a + 1, b, b + 1 });
			}
		}

		FinalizePrimitive(mesh, "Sphere");
		return mesh;
	}

	MeshData MeshFactory::CreatePlane()
	{
		MeshData mesh;
		const glm::vec2 corners[4] = { { -0.5f, 0.5f }, { 0.5f, 0.5f }, { 0.5f, -0.5f }, { -0.5f, -0.5f } };
		for (const glm::vec2& corner : corners)
		{
			Vertex vertex;
			vertex.Position = glm::vec3(corner.x, 0.0f, corner.y);
			vertex.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
			vertex.TexCoord = glm::vec2(corner.x + 0.5f, corner.y + 0.5f);
			mesh.Vertices.push_back(vertex);
		}
		mesh.Indices = { 0, 1, 2, 0, 2, 3 };
		FinalizePrimitive(mesh, "Plane");
		return mesh;
	}

	bool MeshFactory::IsPrimitivePath(std::string_view path)
	{
		return path.starts_with(PrimitivePrefix);
	}

	std::optional<MeshData> MeshFactory::CreatePrimitive(std::string_view path)
	{
		if (!IsPrimitivePath(path))
			return std::nullopt;

		const std::string_view name = path.substr(PrimitivePrefix.size());
		if (name == "Cube")
			return CreateCube();
		if (name == "Sphere")
			return CreateSphere();
		if (name == "Plane")
			return CreatePlane();
		return std::nullopt;
	}

}
