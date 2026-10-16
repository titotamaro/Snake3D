#include "obpch.h"
#include "Ouroboros/Core/Application.h"

#include "Ouroboros/Audio/AudioEngine.h"
#include "Ouroboros/Core/Input.h"
#include "Ouroboros/ImGui/ImGuiLayer.h"
#include "Ouroboros/Renderer/GraphicsContext.h"
#include "Ouroboros/Renderer/Renderer.h"

#include <GLFW/glfw3.h>

namespace Ouroboros {

	Application* Application::s_Instance = nullptr;

	Application::Application(const ApplicationSpecification& specification)
		: m_Specification(specification)
	{
		OB_CORE_VERIFY(!s_Instance, "Only one Application may exist");
		s_Instance = this;

		Log::Init();
		OB_CORE_INFO("Ouroboros {} starting '{}'", OB_VERSION_STRING, specification.Name);

		if (!specification.Headless)
		{
			m_Window = CreateScope<Window>(specification.Window);
			m_Window->SetEventCallback([this](Event& event) { OnEvent(event); });
		}

		GraphicsContextSpecification graphicsSpecification;
		graphicsSpecification.Window = m_Window ? m_Window->GetNativeWindow() : nullptr;
		graphicsSpecification.EnableValidation = specification.EnableValidation;
		graphicsSpecification.VSync = specification.VSync;
		m_GraphicsContext = GraphicsContext::Create(graphicsSpecification);

		if (m_GraphicsContext)
		{
			m_Renderer = CreateScope<Renderer>(*m_GraphicsContext);
			m_CommandList = m_GraphicsContext->GetDevice()->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));

			if (specification.EnableImGui && m_Window)
			{
				ImGuiLayerSpecification imguiSpecification;
				imguiSpecification.EnableDocking = specification.EnableDocking;
				imguiSpecification.IniFile = specification.ImGuiIniFile;
				m_ImGuiLayer = CreateScope<ImGuiLayer>(m_Window->GetNativeWindow(), *m_Renderer, m_GraphicsContext->GetBackBufferFormat(), imguiSpecification);
			}
		}
		else if (specification.RequireGraphics)
		{
			OB_CORE_CRITICAL("A Vulkan 1.3 capable GPU is required");
			return;
		}
		else
		{
			OB_CORE_WARN("Running without graphics");
		}

		AudioEngine::Init(!specification.EnableAudioDevice);
		m_Initialized = true;
	}

	Application::~Application()
	{
		if (m_GraphicsContext)
			m_GraphicsContext->WaitIdle();

		for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it)
		{
			(*it)->OnDetach();
			delete *it;
		}
		m_Layers.clear();

		m_ImGuiLayer.reset();
		m_CommandList = nullptr;
		m_Renderer.reset();
		m_GraphicsContext.reset();
		AudioEngine::Shutdown();
		m_Window.reset();

		Log::Shutdown();
		s_Instance = nullptr;
	}

	void Application::PushLayer(Layer* layer)
	{
		m_Layers.push_back(layer);
		layer->OnAttach();
	}

	void Application::PopLayer(Layer* layer)
	{
		auto it = std::find(m_Layers.begin(), m_Layers.end(), layer);
		if (it == m_Layers.end())
			return;
		layer->OnDetach();
		m_Layers.erase(it);
		delete layer;
	}

	void Application::SubmitPostFrameTask(std::function<void()> task)
	{
		m_PostFrameTasks.push_back(std::move(task));
	}

	void Application::Close(int exitCode)
	{
		m_ExitCode = exitCode;
		m_Running = false;
	}

	bool Application::CaptureBackBuffer(BackBufferCallback callback)
	{
		if (!m_GraphicsContext || m_GraphicsContext->IsHeadless())
			return false;
		m_BackBufferCaptures.push_back(std::move(callback));
		return true;
	}

	nvrhi::IFramebuffer* Application::GetBackBufferFramebuffer() const
	{
		if (!m_Rendering || !m_GraphicsContext)
			return nullptr;
		return m_GraphicsContext->GetCurrentFramebuffer();
	}

	void Application::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent&)
		{
			Close(m_ExitCode);
			return true;
		});

		for (auto it = m_Layers.rbegin(); it != m_Layers.rend() && !event.Handled; ++it)
			(*it)->OnEvent(event);
	}

	int Application::Run()
	{
		if (!m_Initialized)
			return 1;

		Timer frameTimer;
		while (m_Running)
		{
			const Timestep timestep = m_Specification.FixedTimestep > 0.0f ? m_Specification.FixedTimestep : std::min(frameTimer.Elapsed(), 0.25f);
			frameTimer.Reset();

			if (m_Window)
				m_Window->PollEvents();

			m_Rendering = m_GraphicsContext && m_GraphicsContext->BeginFrame();
			if (m_Rendering)
				m_CommandList->open();

			for (Layer* layer : m_Layers)
				layer->OnUpdate(timestep);

			if (m_Rendering)
			{
				for (Layer* layer : m_Layers)
					layer->OnRender(m_CommandList);

				if (m_ImGuiLayer)
				{
					m_ImGuiLayer->Begin();
					for (Layer* layer : m_Layers)
						layer->OnImGuiRender();
					m_ImGuiLayer->End(m_CommandList, m_GraphicsContext->GetCurrentFramebuffer());
				}

				nvrhi::StagingTextureHandle capture;
				nvrhi::ITexture* backBuffer = m_GraphicsContext->GetCurrentBackBuffer();
				if (!m_BackBufferCaptures.empty() && backBuffer)
				{
					nvrhi::TextureDesc desc = backBuffer->getDesc();
					desc.setIsRenderTarget(false).setInitialState(nvrhi::ResourceStates::CopyDest).setKeepInitialState(true).setDebugName("BackBufferCapture");
					desc.isShaderResource = false;
					capture = m_GraphicsContext->GetDevice()->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
					m_CommandList->copyTexture(capture, nvrhi::TextureSlice(), backBuffer, nvrhi::TextureSlice());
				}

				m_CommandList->close();
				m_GraphicsContext->GetDevice()->executeCommandList(m_CommandList);
				m_GraphicsContext->Present();

				if (capture)
				{
					std::vector<BackBufferCallback> callbacks = std::move(m_BackBufferCaptures);
					m_BackBufferCaptures.clear();
					m_PostFrameTasks.push_back([this, capture, callbacks, format = backBuffer->getDesc().format]()
					{
						nvrhi::IDevice* device = m_GraphicsContext->GetDevice();
						device->waitForIdle();
						const nvrhi::TextureDesc& desc = capture->getDesc();
						size_t rowPitch = 0;
						const uint8_t* mapped = static_cast<const uint8_t*>(device->mapStagingTexture(capture, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch));
						std::vector<uint8_t> pixels(static_cast<size_t>(desc.width) * desc.height * 4);
						if (mapped)
						{
							const bool bgra = format == nvrhi::Format::BGRA8_UNORM;
							for (uint32_t y = 0; y < desc.height; y++)
							{
								const uint8_t* source = mapped + y * rowPitch;
								uint8_t* destination = pixels.data() + static_cast<size_t>(y) * desc.width * 4;
								for (uint32_t x = 0; x < desc.width; x++)
								{
									destination[x * 4 + 0] = source[x * 4 + (bgra ? 2 : 0)];
									destination[x * 4 + 1] = source[x * 4 + 1];
									destination[x * 4 + 2] = source[x * 4 + (bgra ? 0 : 2)];
									destination[x * 4 + 3] = 255;
								}
							}
							device->unmapStagingTexture(capture);
						}
						for (const BackBufferCallback& callback : callbacks)
							callback(mapped ? pixels : std::vector<uint8_t>(), desc.width, desc.height);
					});
				}
			}
			else if (m_Window && m_Window->IsMinimized())
			{
				glfwWaitEventsTimeout(0.05);
			}
			m_Rendering = false;

			std::vector<std::function<void()>> tasks = std::move(m_PostFrameTasks);
			m_PostFrameTasks.clear();
			for (const auto& task : tasks)
				task();

			Input::EndFrame();
			AudioEngine::Update();

			m_FrameCount++;
			if (m_Specification.MaxFrames > 0 && m_FrameCount >= m_Specification.MaxFrames)
				m_Running = false;
		}

		OB_CORE_INFO("Application exiting with code {}", m_ExitCode);
		return m_ExitCode;
	}

}
