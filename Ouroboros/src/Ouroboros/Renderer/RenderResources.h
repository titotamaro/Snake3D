#pragma once

#include "Ouroboros/Asset/MeshData.h"
#include "Ouroboros/Core/Base.h"

#include <nvrhi/nvrhi.h>

#include <string>
#include <vector>

namespace Ouroboros {

	// GPU texture (2D, RGBA8, with a full mip chain).
	class Texture2D
	{
	public:
		Texture2D(nvrhi::TextureHandle handle, uint32_t width, uint32_t height)
			: m_Handle(std::move(handle)), m_Width(width), m_Height(height)
		{
		}

		nvrhi::ITexture* GetHandle() const { return m_Handle; }
		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

	private:
		nvrhi::TextureHandle m_Handle;
		uint32_t m_Width;
		uint32_t m_Height;
	};

	// GPU material: constants + textures bound as descriptor set 1 of the forward pass.
	class Material
	{
	public:
		MaterialData Data;
		nvrhi::BufferHandle ConstantBuffer;
		nvrhi::BindingSetHandle BindingSet;
		std::vector<Ref<Texture2D>> Textures; // keeps the bound textures alive
	};

	class Mesh
	{
	public:
		nvrhi::BufferHandle VertexBuffer;
		nvrhi::BufferHandle IndexBuffer;
		std::vector<Submesh> Submeshes;
		std::vector<Ref<Material>> Materials;
		glm::vec3 BoundsMin{ 0.0f };
		glm::vec3 BoundsMax{ 0.0f };
		uint32_t VertexCount = 0;
		uint32_t IndexCount = 0;
	};

	// Image based lighting data generated from an equirectangular HDR image.
	class Environment
	{
	public:
		nvrhi::TextureHandle Radiance;    // full environment, with mips (sky background)
		nvrhi::TextureHandle Irradiance;  // diffuse convolution
		nvrhi::TextureHandle Prefiltered; // specular, one roughness per mip
		uint32_t PrefilteredMipCount = 1;
	};

}
