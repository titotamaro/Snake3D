#include "obpch.h"
#include "Ouroboros/Asset/GltfImporter.h"

#include <cgltf.h>

#include <glm/gtc/type_ptr.hpp>

namespace Ouroboros {

	namespace {

		struct CgltfDataDeleter
		{
			void operator()(cgltf_data* data) const { cgltf_free(data); }
		};

		TextureSource ImportTexture(const cgltf_texture_view& view, const std::filesystem::path& baseDirectory, const std::filesystem::path& modelPath)
		{
			TextureSource source;
			if (!view.texture || !view.texture->image)
				return source;

			const cgltf_image* image = view.texture->image;
			if (image->buffer_view)
			{
				const cgltf_buffer_view* bufferView = image->buffer_view;
				const uint8_t* data = static_cast<const uint8_t*>(bufferView->buffer->data) + bufferView->offset;
				source.EncodedData.assign(data, data + bufferView->size);
				source.Name = modelPath.generic_string() + "#image" + std::to_string(reinterpret_cast<uintptr_t>(image));
			}
			else if (image->uri)
			{
				const std::string uri = image->uri;
				if (uri.starts_with("data:"))
				{
					const size_t comma = uri.find(',');
					if (comma != std::string::npos && uri.find(";base64") != std::string::npos)
					{
						const std::string payload = uri.substr(comma + 1);
						const size_t size = payload.size() * 3 / 4 - (payload.ends_with("==") ? 2 : (payload.ends_with("=") ? 1 : 0));
						void* decoded = nullptr;
						cgltf_options options{};
						if (cgltf_load_buffer_base64(&options, size, payload.c_str(), &decoded) == cgltf_result_success)
						{
							source.EncodedData.assign(static_cast<uint8_t*>(decoded), static_cast<uint8_t*>(decoded) + size);
							std::free(decoded);
							source.Name = modelPath.generic_string() + "#image" + std::to_string(reinterpret_cast<uintptr_t>(image));
						}
					}
				}
				else
				{
					std::string decodedUri = uri;
					cgltf_decode_uri(decodedUri.data());
					decodedUri.resize(std::strlen(decodedUri.c_str()));
					source.Path = baseDirectory / std::filesystem::path(std::u8string(decodedUri.begin(), decodedUri.end()));
				}
			}
			return source;
		}

		MaterialData ImportMaterial(const cgltf_material& material, const std::filesystem::path& baseDirectory, const std::filesystem::path& modelPath)
		{
			MaterialData result;
			result.Name = material.name ? material.name : "Material";

			// glTF defaults (metallic = roughness = 1) apply when the material uses the metallic-roughness model.
			result.MetallicFactor = 1.0f;
			result.RoughnessFactor = 1.0f;
			if (material.has_pbr_metallic_roughness)
			{
				const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;
				result.BaseColorFactor = glm::make_vec4(pbr.base_color_factor);
				result.MetallicFactor = pbr.metallic_factor;
				result.RoughnessFactor = pbr.roughness_factor;
				result.BaseColorTexture = ImportTexture(pbr.base_color_texture, baseDirectory, modelPath);
				result.MetallicRoughnessTexture = ImportTexture(pbr.metallic_roughness_texture, baseDirectory, modelPath);
			}

			result.NormalTexture = ImportTexture(material.normal_texture, baseDirectory, modelPath);
			result.NormalScale = material.normal_texture.texture ? material.normal_texture.scale : 1.0f;
			result.OcclusionTexture = ImportTexture(material.occlusion_texture, baseDirectory, modelPath);
			result.OcclusionStrength = material.occlusion_texture.texture ? material.occlusion_texture.scale : 1.0f;
			result.EmissiveTexture = ImportTexture(material.emissive_texture, baseDirectory, modelPath);
			result.EmissiveFactor = glm::make_vec3(material.emissive_factor);
			if (material.has_emissive_strength)
				result.EmissiveStrength = material.emissive_strength.emissive_strength;
			return result;
		}

		const cgltf_accessor* FindAttribute(const cgltf_primitive& primitive, cgltf_attribute_type type)
		{
			for (cgltf_size i = 0; i < primitive.attributes_count; i++)
			{
				if (primitive.attributes[i].type == type && primitive.attributes[i].index == 0)
					return primitive.attributes[i].data;
			}
			return nullptr;
		}

	}

	std::optional<MeshData> GltfImporter::Import(const std::filesystem::path& path, std::string* error)
	{
		auto fail = [&](const std::string& message) -> std::optional<MeshData>
		{
			if (error)
				*error = path.string() + ": " + message;
			return std::nullopt;
		};

		cgltf_options options{};
		cgltf_data* rawData = nullptr;
		const std::string pathString = path.string();
		if (cgltf_parse_file(&options, pathString.c_str(), &rawData) != cgltf_result_success)
			return fail("could not parse glTF file");
		std::unique_ptr<cgltf_data, CgltfDataDeleter> data(rawData);

		if (cgltf_load_buffers(&options, data.get(), pathString.c_str()) != cgltf_result_success)
			return fail("could not load glTF buffers");
		if (cgltf_validate(data.get()) != cgltf_result_success)
			return fail("glTF validation failed");

		const std::filesystem::path baseDirectory = path.parent_path();

		MeshData mesh;
		std::unordered_map<const cgltf_material*, uint32_t> materialIndices;
		std::optional<uint32_t> defaultMaterialIndex;
		bool missingTangents = false;

		auto getMaterialIndex = [&](const cgltf_material* material) -> uint32_t
		{
			if (!material)
			{
				if (!defaultMaterialIndex)
				{
					defaultMaterialIndex = static_cast<uint32_t>(mesh.Materials.size());
					mesh.Materials.push_back(MaterialData());
				}
				return *defaultMaterialIndex;
			}
			if (auto it = materialIndices.find(material); it != materialIndices.end())
				return it->second;
			const uint32_t index = static_cast<uint32_t>(mesh.Materials.size());
			mesh.Materials.push_back(ImportMaterial(*material, baseDirectory, path));
			materialIndices[material] = index;
			return index;
		};

		auto importNode = [&](const cgltf_node& node)
		{
			if (!node.mesh)
				return;

			float worldMatrix[16];
			cgltf_node_transform_world(&node, worldMatrix);
			const glm::mat4 transform = glm::make_mat4(worldMatrix);
			const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
			const bool flipWinding = glm::determinant(glm::mat3(transform)) < 0.0f;

			for (cgltf_size p = 0; p < node.mesh->primitives_count; p++)
			{
				const cgltf_primitive& primitive = node.mesh->primitives[p];
				if (primitive.type != cgltf_primitive_type_triangles)
				{
					OB_CORE_WARN("glTF '{}': skipping non-triangle primitive", path.filename().string());
					continue;
				}

				const cgltf_accessor* positions = FindAttribute(primitive, cgltf_attribute_type_position);
				if (!positions)
					continue;
				const cgltf_accessor* normals = FindAttribute(primitive, cgltf_attribute_type_normal);
				const cgltf_accessor* tangents = FindAttribute(primitive, cgltf_attribute_type_tangent);
				const cgltf_accessor* texCoords = FindAttribute(primitive, cgltf_attribute_type_texcoord);
				if (!tangents)
					missingTangents = true;

				const uint32_t baseVertex = static_cast<uint32_t>(mesh.Vertices.size());
				const size_t vertexCount = positions->count;
				for (size_t i = 0; i < vertexCount; i++)
				{
					Vertex vertex;
					float values[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

					cgltf_accessor_read_float(positions, i, values, 3);
					vertex.Position = glm::vec3(transform * glm::vec4(values[0], values[1], values[2], 1.0f));

					if (normals && cgltf_accessor_read_float(normals, i, values, 3))
						vertex.Normal = glm::normalize(normalMatrix * glm::vec3(values[0], values[1], values[2]));

					if (tangents && cgltf_accessor_read_float(tangents, i, values, 4))
						vertex.Tangent = glm::vec4(glm::normalize(glm::mat3(transform) * glm::vec3(values[0], values[1], values[2])), values[3] < 0.0f ? -1.0f : 1.0f);

					if (texCoords && cgltf_accessor_read_float(texCoords, i, values, 2))
						vertex.TexCoord = glm::vec2(values[0], values[1]);

					mesh.Vertices.push_back(vertex);
				}

				Submesh submesh;
				submesh.IndexOffset = static_cast<uint32_t>(mesh.Indices.size());
				submesh.MaterialIndex = getMaterialIndex(primitive.material);

				if (primitive.indices)
				{
					for (size_t i = 0; i < primitive.indices->count; i++)
						mesh.Indices.push_back(baseVertex + static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i)));
				}
				else
				{
					for (size_t i = 0; i < vertexCount; i++)
						mesh.Indices.push_back(baseVertex + static_cast<uint32_t>(i));
				}

				submesh.IndexCount = static_cast<uint32_t>(mesh.Indices.size()) - submesh.IndexOffset;
				if (flipWinding)
				{
					for (uint32_t i = submesh.IndexOffset; i + 2 < submesh.IndexOffset + submesh.IndexCount; i += 3)
						std::swap(mesh.Indices[i + 1], mesh.Indices[i + 2]);
				}
				if (submesh.IndexCount > 0)
					mesh.Submeshes.push_back(submesh);
			}
		};

		// Walk the default scene (or every root node if there is none), visiting children recursively.
		std::function<void(const cgltf_node&)> visit = [&](const cgltf_node& node)
		{
			importNode(node);
			for (cgltf_size i = 0; i < node.children_count; i++)
				visit(*node.children[i]);
		};

		const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
		if (scene)
		{
			for (cgltf_size i = 0; i < scene->nodes_count; i++)
				visit(*scene->nodes[i]);
		}
		else
		{
			for (cgltf_size i = 0; i < data->nodes_count; i++)
			{
				if (!data->nodes[i].parent)
					visit(data->nodes[i]);
			}
		}

		if (missingTangents)
			mesh.GenerateTangents();
		mesh.ComputeBounds();

		std::string validationError;
		if (!mesh.Validate(&validationError))
			return fail(validationError);

		return mesh;
	}

}
