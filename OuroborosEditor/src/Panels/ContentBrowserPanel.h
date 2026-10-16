#pragma once

#include <filesystem>
#include <optional>

namespace Ouroboros {

	// Browses the project's asset directory. Files can be dragged ("OB_ASSET_PATH" payload, asset-relative path)
	// into the viewport or onto inspector path fields; double-clicking a file requests it to be opened.
	class ContentBrowserPanel
	{
	public:
		void SetRoot(const std::filesystem::path& root);
		// Returns the asset-relative path of a file that was double-clicked this frame.
		std::optional<std::filesystem::path> OnImGuiRender();

	private:
		std::filesystem::path m_Root;
		std::filesystem::path m_Current;
	};

}
