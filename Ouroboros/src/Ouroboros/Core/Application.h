#pragma once

#include "Ouroboros/Core/Base.h"
#include "Ouroboros/Core/Layer.h"
#include "Ouroboros/Core/Window.h"

#include <functional>
#include <string>
#include <vector>

namespace Ouroboros {

	class GraphicsContext;
	class ImGuiLayer;
	class Renderer;

	struct ApplicationCommandLineArgs
	{
		int Count = 0;
		char** Args = nullptr;

		const char* operator[](int index) const
		{
			OB_CORE_ASSERT(index < Count);
			return Args[index];
		}
	};

	struct ApplicationSpecification
	{
		std::string Name = "Ouroboros Application";
		WindowSpecification Window;
		bool Headless = false;          // no window; renders offscreen (if a GPU is available)
		bool RequireGraphics = true;    // fail to start (instead of running without rendering) when no GPU is available
		bool VSync = true;
		bool EnableValidation = false;  // Vulkan validation layers + NVRHI validation
		bool EnableImGui = true;
		bool EnableDocking = false;
		bool EnableAudioDevice = true;  // false runs audio without an output device
		std::string ImGuiIniFile;
		uint64_t MaxFrames = 0;         // > 0 closes the application after this many frames
		float FixedTimestep = 0.0f;     // > 0 advances every frame by exactly this many seconds (deterministic runs)
		ApplicationCommandLineArgs CommandLineArgs;
	};

	class Application
	{
	public:
		explicit Application(const ApplicationSpecification& specification);
		virtual ~Application();

		Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

		// Runs the main loop until Close() is called. Returns the exit code.
		int Run();
		void Close(int exitCode = 0);

		void PushLayer(Layer* layer);   // the application takes ownership
		void PopLayer(Layer* layer);

		// Runs `task` on the main thread after the current frame has been submitted.
		void SubmitPostFrameTask(std::function<void()> task);

		using BackBufferCallback = std::function<void(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height)>;
		// Captures the next presented window image (scene and UI) as tightly packed RGBA8. Not available headless.
		bool CaptureBackBuffer(BackBufferCallback callback);

		Window* GetWindow() const { return m_Window.get(); }
		GraphicsContext* GetGraphicsContext() const { return m_GraphicsContext.get(); }
		Renderer* GetRenderer() const { return m_Renderer.get(); }
		ImGuiLayer* GetImGuiLayer() const { return m_ImGuiLayer.get(); }
		// Framebuffer of the current swapchain image (nullptr when headless or not rendering this frame).
		nvrhi::IFramebuffer* GetBackBufferFramebuffer() const;

		const ApplicationSpecification& GetSpecification() const { return m_Specification; }
		uint64_t GetFrameCount() const { return m_FrameCount; }
		bool IsInitialized() const { return m_Initialized; }
		int GetExitCode() const { return m_ExitCode; }
		void SetExitCode(int exitCode) { m_ExitCode = exitCode; }

		static Application& Get() { return *s_Instance; }
		static bool HasInstance() { return s_Instance != nullptr; }

	private:
		void OnEvent(Event& event);

	private:
		ApplicationSpecification m_Specification;
		Scope<Window> m_Window;
		Scope<GraphicsContext> m_GraphicsContext;
		Scope<Renderer> m_Renderer;
		Scope<ImGuiLayer> m_ImGuiLayer;
		nvrhi::CommandListHandle m_CommandList;

		std::vector<Layer*> m_Layers;
		std::vector<std::function<void()>> m_PostFrameTasks;
		std::vector<BackBufferCallback> m_BackBufferCaptures;

		bool m_Running = true;
		bool m_Initialized = false;
		bool m_Rendering = false;
		int m_ExitCode = 0;
		uint64_t m_FrameCount = 0;

		static Application* s_Instance;
	};

	// Implemented by each executable.
	Application* CreateApplication(ApplicationCommandLineArgs args);

}
