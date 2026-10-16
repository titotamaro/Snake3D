#pragma once

namespace Ouroboros {

	// Shows the engine and script log (Log::GetBuffer()).
	class ConsolePanel
	{
	public:
		void OnImGuiRender();

	private:
		char m_Filter[128] = {};
		bool m_ShowTrace = false;
		bool m_AutoScroll = true;
	};

}
