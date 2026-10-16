#include "obpch.h"
#include "Ouroboros/ImGui/ImGuiRenderer.h"

#include "Ouroboros/Renderer/Renderer.h"

#include <imgui.h>

namespace Ouroboros {

	ImGuiRenderer::ImGuiRenderer(Renderer& renderer, nvrhi::Format targetFormat)
		: m_Renderer(renderer), m_Device(renderer.GetDevice())
	{
		m_BindingLayout = m_Device->createBindingLayout(nvrhi::BindingLayoutDesc()
			.setVisibility(nvrhi::ShaderType::All)
			.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
			.addItem(nvrhi::BindingLayoutItem::Sampler(0))
			.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(float) * 4)));

		nvrhi::ShaderHandle vertexShader = m_Renderer.GetShader("ImGui.vs");
		const nvrhi::VertexAttributeDesc attributes[] = {
			nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(ImDrawVert, pos)).setElementStride(sizeof(ImDrawVert)),
			nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(ImDrawVert, uv)).setElementStride(sizeof(ImDrawVert)),
			nvrhi::VertexAttributeDesc().setName("COLOR").setFormat(nvrhi::Format::RGBA8_UNORM).setOffset(offsetof(ImDrawVert, col)).setElementStride(sizeof(ImDrawVert))
		};
		m_InputLayout = m_Device->createInputLayout(attributes, static_cast<uint32_t>(std::size(attributes)), vertexShader);

		nvrhi::BlendState blend;
		blend.targets[0].enableBlend()
			.setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
			.setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
			.setSrcBlendAlpha(nvrhi::BlendFactor::One)
			.setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
		nvrhi::RasterState raster;
		raster.setCullNone().setScissorEnable(true);
		nvrhi::DepthStencilState depth;
		depth.disableDepthTest().disableDepthWrite();

		nvrhi::GraphicsPipelineDesc desc;
		desc.setInputLayout(m_InputLayout)
			.setVertexShader(vertexShader)
			.setPixelShader(m_Renderer.GetShader("ImGui.ps"))
			.addBindingLayout(m_BindingLayout)
			.setRenderState(nvrhi::RenderState().setBlendState(blend).setRasterState(raster).setDepthStencilState(depth));
		m_Pipeline = m_Device->createGraphicsPipeline(desc, nvrhi::FramebufferInfo().addColorFormat(targetFormat));
	}

	ImGuiRenderer::~ImGuiRenderer() = default;

	void ImGuiRenderer::CreateFontTexture()
	{
		ImGuiIO& io = ImGui::GetIO();
		unsigned char* pixels = nullptr;
		int width = 0, height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

		m_FontTexture = m_Device->createTexture(nvrhi::TextureDesc()
			.setWidth(static_cast<uint32_t>(width))
			.setHeight(static_cast<uint32_t>(height))
			.setFormat(nvrhi::Format::RGBA8_UNORM)
			.setInitialState(nvrhi::ResourceStates::ShaderResource)
			.setKeepInitialState(true)
			.setDebugName("ImGuiFontAtlas"));

		nvrhi::CommandListHandle commandList = m_Device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
		commandList->open();
		commandList->writeTexture(m_FontTexture, 0, 0, pixels, static_cast<size_t>(width) * 4);
		commandList->close();
		m_Device->executeCommandList(commandList);

		io.Fonts->SetTexID(static_cast<ImTextureID>(ToImTextureID(m_FontTexture)));
	}

	nvrhi::IBindingSet* ImGuiRenderer::GetBindingSet(nvrhi::ITexture* texture)
	{
		if (auto it = m_BindingSets.find(texture); it != m_BindingSets.end())
			return it->second;

		nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(nvrhi::BindingSetDesc()
			.addItem(nvrhi::BindingSetItem::Texture_SRV(0, texture))
			.addItem(nvrhi::BindingSetItem::Sampler(0, m_Renderer.GetLinearClampSampler()))
			.addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(float) * 4)), m_BindingLayout);
		m_BindingSets[texture] = bindingSet;
		return bindingSet;
	}

	void ImGuiRenderer::Render(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer, ImDrawData* drawData)
	{
		// Textures shown by ImGui may be recreated (e.g. viewport resize); rebuild binding sets every frame.
		m_BindingSets.clear();

		if (!drawData || drawData->TotalVtxCount == 0 || drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f)
			return;

		const size_t vertexCount = static_cast<size_t>(drawData->TotalVtxCount);
		const size_t indexCount = static_cast<size_t>(drawData->TotalIdxCount);
		if (!m_VertexBuffer || vertexCount > m_VertexCapacity)
		{
			m_VertexCapacity = vertexCount + 5000;
			m_VertexBuffer = m_Device->createBuffer(nvrhi::BufferDesc()
				.setByteSize(m_VertexCapacity * sizeof(ImDrawVert))
				.setIsVertexBuffer(true)
				.setInitialState(nvrhi::ResourceStates::VertexBuffer)
				.setKeepInitialState(true)
				.setDebugName("ImGuiVertices"));
		}
		if (!m_IndexBuffer || indexCount > m_IndexCapacity)
		{
			m_IndexCapacity = indexCount + 10000;
			m_IndexBuffer = m_Device->createBuffer(nvrhi::BufferDesc()
				.setByteSize(m_IndexCapacity * sizeof(ImDrawIdx))
				.setIsIndexBuffer(true)
				.setInitialState(nvrhi::ResourceStates::IndexBuffer)
				.setKeepInitialState(true)
				.setDebugName("ImGuiIndices"));
		}

		std::vector<ImDrawVert> vertices;
		std::vector<ImDrawIdx> indices;
		vertices.reserve(vertexCount);
		indices.reserve(indexCount);
		for (int i = 0; i < drawData->CmdListsCount; i++)
		{
			const ImDrawList* list = drawData->CmdLists[i];
			vertices.insert(vertices.end(), list->VtxBuffer.Data, list->VtxBuffer.Data + list->VtxBuffer.Size);
			indices.insert(indices.end(), list->IdxBuffer.Data, list->IdxBuffer.Data + list->IdxBuffer.Size);
		}

		commandList->beginMarker("ImGui");
		commandList->writeBuffer(m_VertexBuffer, vertices.data(), vertices.size() * sizeof(ImDrawVert));
		commandList->writeBuffer(m_IndexBuffer, indices.data(), indices.size() * sizeof(ImDrawIdx));

		// ImGui coordinates are Y down; clip space is Y up.
		const float scale[2] = { 2.0f / drawData->DisplaySize.x, -2.0f / drawData->DisplaySize.y };
		const float constants[4] = { scale[0], scale[1], -1.0f - drawData->DisplayPos.x * scale[0], 1.0f - drawData->DisplayPos.y * scale[1] };

		const nvrhi::FramebufferInfoEx& info = framebuffer->getFramebufferInfo();
		const nvrhi::Viewport viewport(static_cast<float>(info.width), static_cast<float>(info.height));
		const ImVec2 clipOffset = drawData->DisplayPos;
		const ImVec2 clipScale = drawData->FramebufferScale;

		uint32_t vertexOffset = 0;
		uint32_t indexOffset = 0;
		for (int i = 0; i < drawData->CmdListsCount; i++)
		{
			const ImDrawList* list = drawData->CmdLists[i];
			for (const ImDrawCmd& command : list->CmdBuffer)
			{
				if (command.UserCallback)
				{
					if (command.UserCallback != ImDrawCallback_ResetRenderState)
						command.UserCallback(list, &command);
					continue;
				}

				const float clipMinX = (command.ClipRect.x - clipOffset.x) * clipScale.x;
				const float clipMinY = (command.ClipRect.y - clipOffset.y) * clipScale.y;
				const float clipMaxX = (command.ClipRect.z - clipOffset.x) * clipScale.x;
				const float clipMaxY = (command.ClipRect.w - clipOffset.y) * clipScale.y;
				const int minX = std::max(0, static_cast<int>(clipMinX));
				const int minY = std::max(0, static_cast<int>(clipMinY));
				const int maxX = std::min(static_cast<int>(info.width), static_cast<int>(clipMaxX));
				const int maxY = std::min(static_cast<int>(info.height), static_cast<int>(clipMaxY));
				if (maxX <= minX || maxY <= minY)
					continue;

				nvrhi::ITexture* texture = reinterpret_cast<nvrhi::ITexture*>(static_cast<uintptr_t>(command.GetTexID()));
				if (!texture)
					continue;

				nvrhi::GraphicsState state;
				state.setPipeline(m_Pipeline)
					.setFramebuffer(framebuffer)
					.setViewport(nvrhi::ViewportState().addViewport(viewport).addScissorRect(nvrhi::Rect(minX, maxX, minY, maxY)))
					.addBindingSet(GetBindingSet(texture));

				nvrhi::VertexBufferBinding vertexBinding;
				vertexBinding.buffer = m_VertexBuffer;
				vertexBinding.slot = 0;
				vertexBinding.offset = 0;
				state.addVertexBuffer(vertexBinding);

				nvrhi::IndexBufferBinding indexBinding;
				indexBinding.buffer = m_IndexBuffer;
				indexBinding.format = sizeof(ImDrawIdx) == 2 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT;
				indexBinding.offset = 0;
				state.setIndexBuffer(indexBinding);

				commandList->setGraphicsState(state);
				commandList->setPushConstants(constants, sizeof(constants));

				nvrhi::DrawArguments arguments;
				arguments.vertexCount = command.ElemCount;
				arguments.startIndexLocation = indexOffset + command.IdxOffset;
				arguments.startVertexLocation = vertexOffset + command.VtxOffset;
				commandList->drawIndexed(arguments);
			}
			vertexOffset += static_cast<uint32_t>(list->VtxBuffer.Size);
			indexOffset += static_cast<uint32_t>(list->IdxBuffer.Size);
		}
		commandList->endMarker();
	}

}
