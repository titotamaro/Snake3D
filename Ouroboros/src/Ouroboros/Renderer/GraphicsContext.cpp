#include "obpch.h"
#include "Ouroboros/Renderer/GraphicsContext.h"

#include <vulkan/vulkan.hpp>

#include <nvrhi/validation.h>
#include <nvrhi/vulkan.h>

#include <GLFW/glfw3.h>

// NVRHI is linked statically, so the program must provide the storage for vulkan.hpp's default dynamic dispatcher.
// GraphicsContext initializes it with the instance and device before NVRHI is created.
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace Ouroboros {

	namespace {

		constexpr const char* ValidationLayerName = "VK_LAYER_KHRONOS_validation";

		class NvrhiMessageCallback final : public nvrhi::IMessageCallback
		{
		public:
			explicit NvrhiMessageCallback(std::atomic<uint32_t>& errorCount)
				: m_ErrorCount(errorCount)
			{
			}

			void message(nvrhi::MessageSeverity severity, const char* messageText) override
			{
				switch (severity)
				{
					case nvrhi::MessageSeverity::Info:
						OB_CORE_TRACE("NVRHI: {}", messageText);
						break;
					case nvrhi::MessageSeverity::Warning:
						OB_CORE_WARN("NVRHI: {}", messageText);
						break;
					case nvrhi::MessageSeverity::Error:
					case nvrhi::MessageSeverity::Fatal:
						OB_CORE_ERROR("NVRHI: {}", messageText);
						m_ErrorCount++;
						break;
				}
			}

		private:
			std::atomic<uint32_t>& m_ErrorCount;
		};

		VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData)
		{
			auto* errorCount = static_cast<std::atomic<uint32_t>*>(userData);
			const char* message = data && data->pMessage ? data->pMessage : "";
			if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
			{
				OB_CORE_ERROR("Vulkan: {}", message);
				(*errorCount)++;
			}
			else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
			{
				OB_CORE_WARN("Vulkan: {}", message);
			}
			return VK_FALSE;
		}

		bool HasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name)
		{
			return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& extension)
			{
				return std::strcmp(extension.extensionName, name) == 0;
			});
		}

	}

	struct GraphicsContext::Impl
	{
		GraphicsContextSpecification Specification;

		Scope<vk::detail::DynamicLoader> Loader;
		VkInstance Instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT Messenger = VK_NULL_HANDLE;
		VkSurfaceKHR Surface = VK_NULL_HANDLE;
		VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
		VkDevice Device = VK_NULL_HANDLE;
		VkQueue Queue = VK_NULL_HANDLE;
		uint32_t QueueFamily = 0;

		std::vector<std::string> InstanceExtensions;
		std::vector<std::string> DeviceExtensions;

		std::atomic<uint32_t> ErrorCount{ 0 };
		Scope<NvrhiMessageCallback> MessageCallback;
		nvrhi::vulkan::DeviceHandle VulkanDevice;
		nvrhi::DeviceHandle NvrhiDevice; // VulkanDevice, or the validation layer wrapping it

		std::string DeviceName;

		// Swapchain
		VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
		VkFormat SwapchainFormat = VK_FORMAT_UNDEFINED;
		VkExtent2D SwapchainExtent{ 0, 0 };
		std::vector<VkImage> SwapchainImages;
		std::vector<nvrhi::TextureHandle> BackBuffers;
		std::vector<nvrhi::FramebufferHandle> Framebuffers;
		std::vector<VkSemaphore> AcquireSemaphores;
		std::vector<VkSemaphore> PresentSemaphores;
		uint32_t AcquireIndex = 0;
		uint32_t ImageIndex = 0;
		bool VSync = true;
		bool SwapchainDirty = false;

		nvrhi::CommandListHandle BarrierCommandList;
		std::vector<nvrhi::EventQueryHandle> FrameQueries;
		std::vector<bool> FrameQuerySubmitted;
		uint64_t FrameIndex = 0;

		~Impl() { Destroy(); }

		bool CreateInstance();
		bool PickPhysicalDevice();
		bool CreateDevice();
		bool CreateSwapchain();
		void DestroySwapchain();
		void Destroy();
	};

	bool GraphicsContext::Impl::CreateInstance()
	{
		auto& dispatcher = VULKAN_HPP_DEFAULT_DISPATCHER;

		try
		{
			Loader = CreateScope<vk::detail::DynamicLoader>();
		}
		catch (const std::exception& exception)
		{
			OB_CORE_ERROR("Vulkan loader not found: {}", exception.what());
			return false;
		}

		auto getInstanceProcAddr = Loader->getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
		if (!getInstanceProcAddr)
		{
			OB_CORE_ERROR("Vulkan loader does not export vkGetInstanceProcAddr");
			return false;
		}
		dispatcher.init(getInstanceProcAddr);

		uint32_t apiVersion = VK_API_VERSION_1_0;
		if (dispatcher.vkEnumerateInstanceVersion)
			dispatcher.vkEnumerateInstanceVersion(&apiVersion);
		if (apiVersion < VK_API_VERSION_1_3)
		{
			OB_CORE_ERROR("Vulkan 1.3 is required, but the installed loader only supports {}.{}", VK_API_VERSION_MAJOR(apiVersion), VK_API_VERSION_MINOR(apiVersion));
			return false;
		}

		uint32_t extensionCount = 0;
		dispatcher.vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		dispatcher.vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());

		if (Specification.Window)
		{
			uint32_t glfwExtensionCount = 0;
			const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
			if (!glfwExtensions)
			{
				OB_CORE_ERROR("GLFW could not find the Vulkan surface extensions");
				return false;
			}
			for (uint32_t i = 0; i < glfwExtensionCount; i++)
				InstanceExtensions.emplace_back(glfwExtensions[i]);
		}

		VkInstanceCreateFlags flags = 0;
		if (HasExtension(availableExtensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
		{
			InstanceExtensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
			flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
		}

		std::vector<const char*> layers;
		bool debugUtils = false;
		if (Specification.EnableValidation)
		{
			uint32_t layerCount = 0;
			dispatcher.vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
			std::vector<VkLayerProperties> availableLayers(layerCount);
			dispatcher.vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
			const bool hasValidation = std::any_of(availableLayers.begin(), availableLayers.end(), [](const VkLayerProperties& layer)
			{
				return std::strcmp(layer.layerName, ValidationLayerName) == 0;
			});
			if (hasValidation)
				layers.push_back(ValidationLayerName);
			else
				OB_CORE_WARN("Vulkan validation requested but {} is not installed", ValidationLayerName);

			if (HasExtension(availableExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
			{
				InstanceExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
				debugUtils = true;
			}
		}

		std::vector<const char*> extensionNames;
		for (const std::string& extension : InstanceExtensions)
			extensionNames.push_back(extension.c_str());

		VkApplicationInfo applicationInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
		applicationInfo.pApplicationName = "Ouroboros";
		applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
		applicationInfo.pEngineName = "Ouroboros";
		applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
		applicationInfo.apiVersion = VK_API_VERSION_1_3;

		VkInstanceCreateInfo createInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		createInfo.flags = flags;
		createInfo.pApplicationInfo = &applicationInfo;
		createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
		createInfo.ppEnabledLayerNames = layers.data();
		createInfo.enabledExtensionCount = static_cast<uint32_t>(extensionNames.size());
		createInfo.ppEnabledExtensionNames = extensionNames.data();

		const VkResult result = dispatcher.vkCreateInstance(&createInfo, nullptr, &Instance);
		if (result != VK_SUCCESS)
		{
			OB_CORE_ERROR("vkCreateInstance failed ({})", static_cast<int>(result));
			return false;
		}
		dispatcher.init(vk::Instance(Instance));

		if (debugUtils)
		{
			VkDebugUtilsMessengerCreateInfoEXT messengerInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
			messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			messengerInfo.pfnUserCallback = VulkanDebugCallback;
			messengerInfo.pUserData = &ErrorCount;
			dispatcher.vkCreateDebugUtilsMessengerEXT(Instance, &messengerInfo, nullptr, &Messenger);
		}

		if (Specification.Window)
		{
			if (glfwCreateWindowSurface(Instance, Specification.Window, nullptr, &Surface) != VK_SUCCESS)
			{
				OB_CORE_ERROR("Could not create a Vulkan surface for the window");
				return false;
			}
		}

		return true;
	}

	bool GraphicsContext::Impl::PickPhysicalDevice()
	{
		auto& dispatcher = VULKAN_HPP_DEFAULT_DISPATCHER;

		uint32_t deviceCount = 0;
		dispatcher.vkEnumeratePhysicalDevices(Instance, &deviceCount, nullptr);
		std::vector<VkPhysicalDevice> devices(deviceCount);
		dispatcher.vkEnumeratePhysicalDevices(Instance, &deviceCount, devices.data());

		int bestScore = -1;
		for (VkPhysicalDevice device : devices)
		{
			VkPhysicalDeviceProperties properties;
			dispatcher.vkGetPhysicalDeviceProperties(device, &properties);
			if (properties.apiVersion < VK_API_VERSION_1_3)
				continue;

			VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
			VkPhysicalDeviceVulkan12Features features12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
			features12.pNext = &features13;
			VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			features.pNext = &features12;
			dispatcher.vkGetPhysicalDeviceFeatures2(device, &features);
			if (!features13.dynamicRendering || !features13.synchronization2 || !features13.shaderDemoteToHelperInvocation || !features12.timelineSemaphore)
				continue;

			uint32_t extensionCount = 0;
			dispatcher.vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
			std::vector<VkExtensionProperties> extensions(extensionCount);
			dispatcher.vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());
			if (Surface && !HasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
				continue;

			uint32_t familyCount = 0;
			dispatcher.vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
			std::vector<VkQueueFamilyProperties> families(familyCount);
			dispatcher.vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());

			std::optional<uint32_t> queueFamily;
			for (uint32_t i = 0; i < familyCount; i++)
			{
				const VkQueueFlags required = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
				if ((families[i].queueFlags & required) != required)
					continue;
				if (Surface)
				{
					VkBool32 presentSupported = VK_FALSE;
					dispatcher.vkGetPhysicalDeviceSurfaceSupportKHR(device, i, Surface, &presentSupported);
					if (!presentSupported)
						continue;
				}
				queueFamily = i;
				break;
			}
			if (!queueFamily)
				continue;

			int score = 1;
			if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
				score += 1000;
			else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
				score += 100;

			if (score > bestScore)
			{
				bestScore = score;
				PhysicalDevice = device;
				QueueFamily = *queueFamily;
				DeviceName = properties.deviceName;
			}
		}

		if (!PhysicalDevice)
		{
			OB_CORE_ERROR("No Vulkan 1.3 device with dynamic rendering, synchronization2 and timeline semaphores was found");
			return false;
		}

		OB_CORE_INFO("Using GPU: {}", DeviceName);
		return true;
	}

	bool GraphicsContext::Impl::CreateDevice()
	{
		auto& dispatcher = VULKAN_HPP_DEFAULT_DISPATCHER;

		uint32_t extensionCount = 0;
		dispatcher.vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &extensionCount, nullptr);
		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		dispatcher.vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &extensionCount, availableExtensions.data());

		if (Surface)
			DeviceExtensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		if (HasExtension(availableExtensions, "VK_KHR_portability_subset"))
			DeviceExtensions.emplace_back("VK_KHR_portability_subset");

		VkPhysicalDeviceVulkan13Features supported13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		VkPhysicalDeviceVulkan12Features supported12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		supported12.pNext = &supported13;
		VkPhysicalDeviceFeatures2 supported{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		supported.pNext = &supported12;
		dispatcher.vkGetPhysicalDeviceFeatures2(PhysicalDevice, &supported);

		VkPhysicalDeviceVulkan13Features enabled13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		enabled13.dynamicRendering = VK_TRUE;
		enabled13.synchronization2 = VK_TRUE;
		enabled13.maintenance4 = supported13.maintenance4;
		enabled13.shaderDemoteToHelperInvocation = supported13.shaderDemoteToHelperInvocation; // HLSL `discard`

		VkPhysicalDeviceVulkan12Features enabled12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		enabled12.pNext = &enabled13;
		enabled12.timelineSemaphore = VK_TRUE;
		enabled12.bufferDeviceAddress = supported12.bufferDeviceAddress;

		VkPhysicalDeviceFeatures2 enabled{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		enabled.pNext = &enabled12;
		enabled.features.samplerAnisotropy = supported.features.samplerAnisotropy;
		enabled.features.fillModeNonSolid = supported.features.fillModeNonSolid;
		enabled.features.shaderImageGatherExtended = supported.features.shaderImageGatherExtended;
		enabled.features.independentBlend = supported.features.independentBlend;
		enabled.features.depthClamp = supported.features.depthClamp;

		const float priority = 1.0f;
		VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		queueInfo.queueFamilyIndex = QueueFamily;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &priority;

		std::vector<const char*> extensionNames;
		for (const std::string& extension : DeviceExtensions)
			extensionNames.push_back(extension.c_str());

		VkDeviceCreateInfo createInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		createInfo.pNext = &enabled;
		createInfo.queueCreateInfoCount = 1;
		createInfo.pQueueCreateInfos = &queueInfo;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(extensionNames.size());
		createInfo.ppEnabledExtensionNames = extensionNames.data();

		const VkResult result = dispatcher.vkCreateDevice(PhysicalDevice, &createInfo, nullptr, &Device);
		if (result != VK_SUCCESS)
		{
			OB_CORE_ERROR("vkCreateDevice failed ({})", static_cast<int>(result));
			return false;
		}
		dispatcher.init(vk::Device(Device));
		dispatcher.vkGetDeviceQueue(Device, QueueFamily, 0, &Queue);

		std::vector<const char*> instanceExtensionNames;
		for (const std::string& extension : InstanceExtensions)
			instanceExtensionNames.push_back(extension.c_str());

		MessageCallback = CreateScope<NvrhiMessageCallback>(ErrorCount);

		nvrhi::vulkan::DeviceDesc deviceDesc;
		deviceDesc.errorCB = MessageCallback.get();
		deviceDesc.instance = Instance;
		deviceDesc.physicalDevice = PhysicalDevice;
		deviceDesc.device = Device;
		deviceDesc.graphicsQueue = Queue;
		deviceDesc.graphicsQueueIndex = static_cast<int>(QueueFamily);
		deviceDesc.instanceExtensions = instanceExtensionNames.data();
		deviceDesc.numInstanceExtensions = instanceExtensionNames.size();
		deviceDesc.deviceExtensions = extensionNames.data();
		deviceDesc.numDeviceExtensions = extensionNames.size();
		deviceDesc.bufferDeviceAddressSupported = supported12.bufferDeviceAddress == VK_TRUE;

		VulkanDevice = nvrhi::vulkan::createDevice(deviceDesc);
		if (!VulkanDevice)
		{
			OB_CORE_ERROR("Could not create the NVRHI Vulkan device");
			return false;
		}

		NvrhiDevice = VulkanDevice;
		if (Specification.EnableValidation)
			NvrhiDevice = nvrhi::validation::createValidationLayer(VulkanDevice);

		BarrierCommandList = NvrhiDevice->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
		for (uint32_t i = 0; i < Specification.FramesInFlight; i++)
		{
			FrameQueries.push_back(NvrhiDevice->createEventQuery());
			FrameQuerySubmitted.push_back(false);
		}
		return true;
	}

	bool GraphicsContext::Impl::CreateSwapchain()
	{
		auto& dispatcher = VULKAN_HPP_DEFAULT_DISPATCHER;

		VkSurfaceCapabilitiesKHR capabilities;
		dispatcher.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(PhysicalDevice, Surface, &capabilities);

		uint32_t formatCount = 0;
		dispatcher.vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &formatCount, nullptr);
		std::vector<VkSurfaceFormatKHR> formats(formatCount);
		dispatcher.vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &formatCount, formats.data());

		// Ouroboros writes gamma-encoded values itself, so a UNORM (non-sRGB) swapchain is used.
		VkSurfaceFormatKHR surfaceFormat = formats.empty() ? VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } : formats.front();
		for (const VkSurfaceFormatKHR& format : formats)
		{
			if ((format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM) &&
				format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				surfaceFormat = format;
				break;
			}
		}

		uint32_t modeCount = 0;
		dispatcher.vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Surface, &modeCount, nullptr);
		std::vector<VkPresentModeKHR> modes(modeCount);
		dispatcher.vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Surface, &modeCount, modes.data());

		VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
		if (!VSync)
		{
			if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end())
				presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
			else if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end())
				presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
		}

		VkExtent2D extent = capabilities.currentExtent;
		if (extent.width == UINT32_MAX)
		{
			int width = 0, height = 0;
			glfwGetFramebufferSize(Specification.Window, &width, &height);
			extent.width = std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
			extent.height = std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
		}
		if (extent.width == 0 || extent.height == 0)
			return false;

		uint32_t imageCount = std::max(capabilities.minImageCount + 1, 3u);
		if (capabilities.maxImageCount > 0)
			imageCount = std::min(imageCount, capabilities.maxImageCount);

		VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		if (!(capabilities.supportedCompositeAlpha & compositeAlpha))
		{
			for (VkCompositeAlphaFlagBitsKHR candidate : { VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR })
			{
				if (capabilities.supportedCompositeAlpha & candidate)
				{
					compositeAlpha = candidate;
					break;
				}
			}
		}

		VkSwapchainCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
		createInfo.surface = Surface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.preTransform = capabilities.currentTransform;
		createInfo.compositeAlpha = compositeAlpha;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = Swapchain;

		VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
		const VkResult result = dispatcher.vkCreateSwapchainKHR(Device, &createInfo, nullptr, &newSwapchain);
		if (Swapchain)
			dispatcher.vkDestroySwapchainKHR(Device, Swapchain, nullptr);
		Swapchain = newSwapchain;
		if (result != VK_SUCCESS)
		{
			OB_CORE_ERROR("vkCreateSwapchainKHR failed ({})", static_cast<int>(result));
			return false;
		}

		SwapchainFormat = surfaceFormat.format;
		SwapchainExtent = extent;

		uint32_t swapchainImageCount = 0;
		dispatcher.vkGetSwapchainImagesKHR(Device, Swapchain, &swapchainImageCount, nullptr);
		SwapchainImages.resize(swapchainImageCount);
		dispatcher.vkGetSwapchainImagesKHR(Device, Swapchain, &swapchainImageCount, SwapchainImages.data());

		const nvrhi::Format format = SwapchainFormat == VK_FORMAT_R8G8B8A8_UNORM ? nvrhi::Format::RGBA8_UNORM : nvrhi::Format::BGRA8_UNORM;
		for (uint32_t i = 0; i < swapchainImageCount; i++)
		{
			nvrhi::TextureDesc textureDesc;
			textureDesc.setWidth(extent.width)
				.setHeight(extent.height)
				.setFormat(format)
				.setIsRenderTarget(true)
				.setInitialState(nvrhi::ResourceStates::Present)
				.setKeepInitialState(true)
				.setDebugName("SwapchainImage" + std::to_string(i));
			textureDesc.isShaderResource = false;

			nvrhi::TextureHandle texture = NvrhiDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, nvrhi::Object(SwapchainImages[i]), textureDesc);
			BackBuffers.push_back(texture);
			Framebuffers.push_back(NvrhiDevice->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(texture)));
		}

		VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		const size_t acquireCount = std::max<size_t>(swapchainImageCount, Specification.FramesInFlight) + 1;
		for (size_t i = 0; i < acquireCount; i++)
		{
			VkSemaphore semaphore = VK_NULL_HANDLE;
			dispatcher.vkCreateSemaphore(Device, &semaphoreInfo, nullptr, &semaphore);
			AcquireSemaphores.push_back(semaphore);
		}
		for (uint32_t i = 0; i < swapchainImageCount; i++)
		{
			VkSemaphore semaphore = VK_NULL_HANDLE;
			dispatcher.vkCreateSemaphore(Device, &semaphoreInfo, nullptr, &semaphore);
			PresentSemaphores.push_back(semaphore);
		}
		AcquireIndex = 0;
		SwapchainDirty = false;

		OB_CORE_TRACE("Swapchain created: {} x {}, {} images, vsync {}", extent.width, extent.height, swapchainImageCount, VSync);
		return true;
	}

	void GraphicsContext::Impl::DestroySwapchain()
	{
		auto& dispatcher = VULKAN_HPP_DEFAULT_DISPATCHER;
		if (NvrhiDevice)
			NvrhiDevice->waitForIdle();

		Framebuffers.clear();
		BackBuffers.clear();
		SwapchainImages.clear();

		for (VkSemaphore semaphore : AcquireSemaphores)
			dispatcher.vkDestroySemaphore(Device, semaphore, nullptr);
		for (VkSemaphore semaphore : PresentSemaphores)
			dispatcher.vkDestroySemaphore(Device, semaphore, nullptr);
		AcquireSemaphores.clear();
		PresentSemaphores.clear();
	}

	void GraphicsContext::Impl::Destroy()
	{
		auto& dispatcher = VULKAN_HPP_DEFAULT_DISPATCHER;

		if (Device)
		{
			DestroySwapchain();
			if (Swapchain)
			{
				dispatcher.vkDestroySwapchainKHR(Device, Swapchain, nullptr);
				Swapchain = VK_NULL_HANDLE;
			}

			FrameQueries.clear();
			BarrierCommandList = nullptr;
			NvrhiDevice = nullptr;
			VulkanDevice = nullptr;

			dispatcher.vkDestroyDevice(Device, nullptr);
			Device = VK_NULL_HANDLE;
		}

		if (Instance)
		{
			if (Surface)
				dispatcher.vkDestroySurfaceKHR(Instance, Surface, nullptr);
			if (Messenger)
				dispatcher.vkDestroyDebugUtilsMessengerEXT(Instance, Messenger, nullptr);
			dispatcher.vkDestroyInstance(Instance, nullptr);
			Instance = VK_NULL_HANDLE;
			Surface = VK_NULL_HANDLE;
			Messenger = VK_NULL_HANDLE;
		}
	}

	// ----------------------------------------------------------------------------------------------------------------

	Scope<GraphicsContext> GraphicsContext::Create(const GraphicsContextSpecification& specification)
	{
		Scope<GraphicsContext> context(new GraphicsContext());
		context->m_Impl = CreateScope<Impl>();
		Impl& impl = *context->m_Impl;
		impl.Specification = specification;
		impl.Specification.FramesInFlight = std::max(1u, specification.FramesInFlight);
		impl.VSync = specification.VSync;

		if (!impl.CreateInstance() || !impl.PickPhysicalDevice() || !impl.CreateDevice())
			return nullptr;

		if (impl.Surface && !impl.CreateSwapchain())
		{
			// A zero-sized (minimized) window at startup is not fatal; the swapchain is created on the first frame.
			impl.SwapchainDirty = true;
		}

		return context;
	}

	GraphicsContext::~GraphicsContext() = default;

	nvrhi::IDevice* GraphicsContext::GetDevice() const
	{
		return m_Impl->NvrhiDevice.Get();
	}

	bool GraphicsContext::BeginFrame()
	{
		Impl& impl = *m_Impl;
		auto& dispatcher = VULKAN_HPP_DEFAULT_DISPATCHER;

		// Frame pacing: wait until the GPU finished the frame that used this slot.
		const size_t slot = impl.FrameIndex % impl.FrameQueries.size();
		if (impl.FrameQuerySubmitted[slot])
		{
			impl.NvrhiDevice->waitEventQuery(impl.FrameQueries[slot]);
			impl.NvrhiDevice->resetEventQuery(impl.FrameQueries[slot]);
			impl.FrameQuerySubmitted[slot] = false;
		}

		if (!impl.Surface)
			return true;

		int width = 0, height = 0;
		glfwGetFramebufferSize(impl.Specification.Window, &width, &height);
		if (width == 0 || height == 0)
			return false;

		if (impl.SwapchainDirty || static_cast<uint32_t>(width) != impl.SwapchainExtent.width || static_cast<uint32_t>(height) != impl.SwapchainExtent.height)
		{
			impl.DestroySwapchain();
			if (!impl.CreateSwapchain())
				return false;
		}

		for (int attempt = 0; attempt < 2; attempt++)
		{
			VkSemaphore semaphore = impl.AcquireSemaphores[impl.AcquireIndex];
			const VkResult result = dispatcher.vkAcquireNextImageKHR(impl.Device, impl.Swapchain, UINT64_MAX, semaphore, VK_NULL_HANDLE, &impl.ImageIndex);
			if (result == VK_ERROR_OUT_OF_DATE_KHR)
			{
				impl.DestroySwapchain();
				if (!impl.CreateSwapchain())
					return false;
				continue;
			}
			if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
			{
				OB_CORE_ERROR("vkAcquireNextImageKHR failed ({})", static_cast<int>(result));
				return false;
			}
			if (result == VK_SUBOPTIMAL_KHR)
				impl.SwapchainDirty = true;

			impl.AcquireIndex = (impl.AcquireIndex + 1) % static_cast<uint32_t>(impl.AcquireSemaphores.size());
			impl.VulkanDevice->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, semaphore, 0);
			return true;
		}
		return false;
	}

	void GraphicsContext::Present()
	{
		Impl& impl = *m_Impl;
		auto& dispatcher = VULKAN_HPP_DEFAULT_DISPATCHER;

		const size_t slot = impl.FrameIndex % impl.FrameQueries.size();

		if (impl.Surface && impl.Swapchain && !impl.PresentSemaphores.empty())
		{
			VkSemaphore presentSemaphore = impl.PresentSemaphores[impl.ImageIndex];
			impl.VulkanDevice->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, presentSemaphore, 0);

			// The signal is attached to the next submission.
			impl.BarrierCommandList->open();
			impl.BarrierCommandList->close();
			impl.NvrhiDevice->executeCommandList(impl.BarrierCommandList);

			VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
			presentInfo.waitSemaphoreCount = 1;
			presentInfo.pWaitSemaphores = &presentSemaphore;
			presentInfo.swapchainCount = 1;
			presentInfo.pSwapchains = &impl.Swapchain;
			presentInfo.pImageIndices = &impl.ImageIndex;
			const VkResult result = dispatcher.vkQueuePresentKHR(impl.Queue, &presentInfo);
			if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
				impl.SwapchainDirty = true;
			else if (result != VK_SUCCESS)
				OB_CORE_ERROR("vkQueuePresentKHR failed ({})", static_cast<int>(result));
		}

		impl.NvrhiDevice->setEventQuery(impl.FrameQueries[slot], nvrhi::CommandQueue::Graphics);
		impl.FrameQuerySubmitted[slot] = true;
		impl.FrameIndex++;
		impl.NvrhiDevice->runGarbageCollection();
	}

	bool GraphicsContext::IsHeadless() const
	{
		return m_Impl->Surface == VK_NULL_HANDLE;
	}

	nvrhi::ITexture* GraphicsContext::GetCurrentBackBuffer() const
	{
		if (m_Impl->BackBuffers.empty())
			return nullptr;
		return m_Impl->BackBuffers[m_Impl->ImageIndex];
	}

	nvrhi::IFramebuffer* GraphicsContext::GetCurrentFramebuffer() const
	{
		if (m_Impl->Framebuffers.empty())
			return nullptr;
		return m_Impl->Framebuffers[m_Impl->ImageIndex];
	}

	uint32_t GraphicsContext::GetBackBufferWidth() const
	{
		return m_Impl->SwapchainExtent.width;
	}

	uint32_t GraphicsContext::GetBackBufferHeight() const
	{
		return m_Impl->SwapchainExtent.height;
	}

	nvrhi::Format GraphicsContext::GetBackBufferFormat() const
	{
		return m_Impl->SwapchainFormat == VK_FORMAT_R8G8B8A8_UNORM ? nvrhi::Format::RGBA8_UNORM : nvrhi::Format::BGRA8_UNORM;
	}

	void GraphicsContext::SetVSync(bool enabled)
	{
		if (m_Impl->VSync == enabled)
			return;
		m_Impl->VSync = enabled;
		m_Impl->SwapchainDirty = true;
	}

	bool GraphicsContext::IsVSync() const
	{
		return m_Impl->VSync;
	}

	void GraphicsContext::WaitIdle()
	{
		if (m_Impl->NvrhiDevice)
			m_Impl->NvrhiDevice->waitForIdle();
	}

	const std::string& GraphicsContext::GetDeviceName() const
	{
		return m_Impl->DeviceName;
	}

	uint32_t GraphicsContext::GetValidationErrorCount() const
	{
		return m_Impl->ErrorCount.load();
	}

}
