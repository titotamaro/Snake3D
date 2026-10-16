#pragma once

#include "Ouroboros/Core/Event.h"
#include "Ouroboros/Core/Timestep.h"

#include <nvrhi/nvrhi.h>

#include <string>

namespace Ouroboros {

	// An application layer. Per frame the Application calls OnUpdate, then OnRender (with an open command list, only when
	// a frame is being rendered), then OnImGuiRender (only when ImGui is enabled).
	class Layer
	{
	public:
		explicit Layer(const std::string& name = "Layer")
			: m_DebugName(name)
		{
		}
		virtual ~Layer() = default;

		virtual void OnAttach() {}
		virtual void OnDetach() {}
		virtual void OnUpdate(Timestep) {}
		virtual void OnRender(nvrhi::ICommandList*) {}
		virtual void OnImGuiRender() {}
		virtual void OnEvent(Event&) {}

		const std::string& GetName() const { return m_DebugName; }

	private:
		std::string m_DebugName;
	};

}
