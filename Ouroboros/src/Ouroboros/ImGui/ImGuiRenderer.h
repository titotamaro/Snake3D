#pragma once

#include <nvrhi/nvrhi.h>

#include <unordered_map>

struct ImDrawData;

namespace Ouroboros {

	class Renderer;

	// Dear ImGui rendering backend for NVRHI. ImTextureID values are nvrhi::ITexture pointers (see ToImTextureID).
	class ImGuiRenderer
	{
	public:
		ImGuiRenderer(Renderer& renderer, nvrhi::Format targetFormat);
		~ImGuiRenderer();

		// Uploads the current font atlas (call after adding fonts).
		void CreateFontTexture();
		void Render(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer, ImDrawData* drawData);

	private:
		nvrhi::IBindingSet* GetBindingSet(nvrhi::ITexture* texture);

	private:
		Renderer& m_Renderer;
		nvrhi::IDevice* m_Device;
		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::InputLayoutHandle m_InputLayout;
		nvrhi::GraphicsPipelineHandle m_Pipeline;
		nvrhi::TextureHandle m_FontTexture;
		nvrhi::BufferHandle m_VertexBuffer;
		nvrhi::BufferHandle m_IndexBuffer;
		size_t m_VertexCapacity = 0;
		size_t m_IndexCapacity = 0;
		std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> m_BindingSets;
	};

	inline unsigned long long ToImTextureID(nvrhi::ITexture* texture)
	{
		return static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(texture));
	}

}
