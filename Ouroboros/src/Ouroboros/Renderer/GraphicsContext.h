#pragma once

#include "Ouroboros/Core/Base.h"

#include <nvrhi/nvrhi.h>

#include <string>
#include <vector>

struct GLFWwindow;

namespace Ouroboros {

	struct GraphicsContextSpecification
	{
		GLFWwindow* Window = nullptr; // nullptr creates a headless context (offscreen rendering only)
		bool EnableValidation = false; // Vulkan validation layers + NVRHI validation
		bool VSync = true;
		uint32_t FramesInFlight = 2;
	};

	// Owns the Vulkan instance/device, the NVRHI device, the swapchain and frame synchronization.
	class GraphicsContext
	{
	public:
		// Returns nullptr (and logs why) when no suitable Vulkan 1.3 device is available.
		static Scope<GraphicsContext> Create(const GraphicsContextSpecification& specification);
		~GraphicsContext();

		GraphicsContext(const GraphicsContext&) = delete;
		GraphicsContext& operator=(const GraphicsContext&) = delete;

		nvrhi::IDevice* GetDevice() const;

		// Waits for a free frame slot and acquires the next swapchain image. Returns false when nothing can be rendered
		// (minimized window); the caller should skip rendering this frame.
		bool BeginFrame();
		// Submits pending work and presents the current swapchain image.
		void Present();

		bool IsHeadless() const;
		nvrhi::ITexture* GetCurrentBackBuffer() const;
		nvrhi::IFramebuffer* GetCurrentFramebuffer() const;
		uint32_t GetBackBufferWidth() const;
		uint32_t GetBackBufferHeight() const;
		nvrhi::Format GetBackBufferFormat() const;

		void SetVSync(bool enabled);
		bool IsVSync() const;

		void WaitIdle();

		const std::string& GetDeviceName() const;
		// Number of errors reported by the Vulkan validation layers and NVRHI since creation.
		uint32_t GetValidationErrorCount() const;

	private:
		GraphicsContext() = default;

		struct Impl;
		Scope<Impl> m_Impl;
	};

}
