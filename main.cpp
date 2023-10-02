#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>


#define VK_NO_PROTOTYPES    // volkを使用するための定義

// volk をインクルード.
// VOLK_IMPLEMENTATIONを定義して実装も含める.
#define VOLK_IMPLEMENTATION
#define VK_USE_PLATFORM_WIN32_KHR
#include "Volk/volk.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3.h"
#include "GLFW/glfw3native.h"

#include <cstdint>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <array>
#include <format>

// コンパイル済みのシェーダーをヘッダファイルにしたもの.
#include "vertexShader.h"
#include "fragementShader.h"

class FullscreenExclusiveApp
{
private:
	static void KeyProcessCallback(GLFWwindow* window, int, int, int, int);
	static void WindowSizeCallback(GLFWwindow* window, int width, int height);
public:
	bool Initialize()
	{
		// GLFWの初期化.
		if (!glfwInit()) {
			return false;
		}

		// GLFWでVulkanを使用することを指定.
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		// ウィンドウを生成.
		m_window = glfwCreateWindow(1280, 720, "Sample", nullptr, nullptr);
		if (!m_window)
		{
			return false;
		}
		glfwSetWindowUserPointer(m_window, this);
		glfwSetKeyCallback(m_window, KeyProcessCallback);
		glfwSetWindowSizeCallback(m_window, WindowSizeCallback);


		m_swapchainContext.surfaceFullScreenExclusiveInfo.sType = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT;
		m_swapchainContext.surfaceFullScreenExclusiveWin32Info.sType = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT;
		m_swapchainContext.surfaceFullScreenExclusiveInfo.pNext = &m_swapchainContext.surfaceFullScreenExclusiveWin32Info;

		auto hwnd = glfwGetWin32Window(m_window);
		m_swapchainContext.surfaceFullScreenExclusiveInfo.fullScreenExclusive = VK_FULL_SCREEN_EXCLUSIVE_DEFAULT_EXT;
		m_swapchainContext.surfaceFullScreenExclusiveWin32Info.hmonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

		if (!InitializeVulkanInstance())
		{
			return false;
		}

		if (glfwCreateWindowSurface(m_vkInstance, m_window, nullptr, &m_surface) != VK_SUCCESS)
		{
			return false;
		}


		if (!InitializeVulkanDevice())
		{
			return false;
		}

		InitializeSwapchain();
		InitializeRenderPass();
		InitializePipeline();
		InitializeFramebuffers();

		VkSemaphoreCreateInfo semaphoreCreateInfo{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
		};
		vkCreateSemaphore(m_vkDevice, &semaphoreCreateInfo, nullptr, &m_semRenderComplete);
		vkCreateSemaphore(m_vkDevice, &semaphoreCreateInfo, nullptr, &m_semPresentComplete);

		// 現在のウィンドウ状態を保存.
		m_windowStyle = GetWindowLongA(hwnd, GWL_STYLE);
		m_windowStyleEx = GetWindowLongA(hwnd, GWL_EXSTYLE);

		GetWindowPlacement(hwnd, &m_wpc);
		return true;
	}

	void Run()
	{
		while (glfwWindowShouldClose(m_window) == GLFW_FALSE)
		{
			glfwPollEvents();

			uint32_t index = 0;
			auto res = AcquireNextImage(&index);

			if (res != VK_SUCCESS)
			{
				vkQueueWaitIdle(m_deviceQueue);
				return;
			}

			auto& frame = m_frames[index];
			VkCommandBufferBeginInfo beginInfo{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			};
			vkBeginCommandBuffer(frame.commandBuffer, &beginInfo);
			
			VkClearValue clearValue{};
			clearValue.color = { { 1.0f, 0.6f, 0.5f, 1.0f,} };
			VkRenderPassBeginInfo renderPassBI{};
			renderPassBI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassBI.renderPass = m_renderPass;
			renderPassBI.framebuffer = m_swapchainContext.framebuffers[index];
			renderPassBI.renderArea.offset = VkOffset2D{ 0, 0 };
			renderPassBI.renderArea.extent = m_swapchainContext.dimensions;
			renderPassBI.pClearValues = &clearValue;
			renderPassBI.clearValueCount = 1;
			vkCmdBeginRenderPass(frame.commandBuffer, &renderPassBI, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

			VkViewport viewport{
				.x = 0,
				.y = 0,
				.width = float(m_swapchainContext.dimensions.width),
				.height = float(m_swapchainContext.dimensions.height),
				.minDepth = 0.0f,
				.maxDepth = 1.0f,
			};
			VkRect2D scissor{
				.offset = { 0, 0 },
				.extent = m_swapchainContext.dimensions,
			};
			vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
			vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
			vkCmdDraw(frame.commandBuffer, 3, 1, 0, 0);

			vkCmdEndRenderPass(frame.commandBuffer);

			vkEndCommandBuffer(frame.commandBuffer);

			VkPipelineStageFlags wait_stage{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
			VkSubmitInfo submitInfo{
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.waitSemaphoreCount = 1,
				.pWaitSemaphores = &m_semPresentComplete,
				.pWaitDstStageMask = &wait_stage,
				.commandBufferCount = 1,
				.pCommandBuffers = &frame.commandBuffer,
				.signalSemaphoreCount = 1,
				.pSignalSemaphores = &m_semRenderComplete,
			};
			vkQueueSubmit(m_deviceQueue, 1, &submitInfo, frame.queueSubmitFence);

			res = PresentImage(index);
			if (res != VK_SUCCESS)
			{
				OutputDebugStringA("Present Failed.\n");
				if (res == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)
				{
					MessageBoxA(NULL, "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT", "Error", MB_OK);
					return;
				}
			}
		}
	}

	void Shutdown()
	{
		vkDeviceWaitIdle(m_vkDevice);

		if (m_pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(m_vkDevice, m_pipeline, nullptr);
			m_pipeline = VK_NULL_HANDLE;
		}
		if (m_pipelineLayout != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(m_vkDevice, m_pipelineLayout, nullptr);
			m_pipelineLayout = VK_NULL_HANDLE;
		}

		TeardownFramebuffers();
		for (auto& frame : m_frames)
		{
			TeardownPerFrame(frame);
		}
		m_frames.clear();

		vkDestroySemaphore(m_vkDevice, m_semRenderComplete, nullptr);
		vkDestroySemaphore(m_vkDevice, m_semPresentComplete, nullptr);
		m_semRenderComplete = VK_NULL_HANDLE;
		m_semPresentComplete = VK_NULL_HANDLE;

		if (m_renderPass != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(m_vkDevice, m_renderPass, nullptr);
		}

		for (auto& view : m_swapchainContext.imageViews)
		{
			vkDestroyImageView(m_vkDevice, view, nullptr);
		}
		m_swapchainContext.imageViews.clear();
		if (m_swapchainContext.swapchain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(m_vkDevice, m_swapchainContext.swapchain, nullptr);
		}
		if (m_surface != VK_NULL_HANDLE)
		{
			vkDestroySurfaceKHR(m_vkInstance, m_surface, nullptr);
			m_surface = VK_NULL_HANDLE;
		}

		if (m_vkDevice != VK_NULL_HANDLE)
		{
			vkDestroyDevice(m_vkDevice, nullptr);
			m_vkDevice = VK_NULL_HANDLE;
		}

		if (m_debugUtils)
		{
			vkDestroyDebugUtilsMessengerEXT(m_vkInstance, m_debugUtils, nullptr);
			m_debugUtils = VK_NULL_HANDLE;
		}

		if (m_window)
		{
			glfwDestroyWindow(m_window);
			m_window = nullptr;
		}
	}

private:
	bool InitializeVulkanInstance()
	{
		if (volkInitialize() != VK_SUCCESS)
		{
			OutputDebugStringA("volkInitialize failed.\n");
			return false;
		}
		uint32_t instanceExtensionCount;
		vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, nullptr);
		std::vector<VkExtensionProperties> instanceExtensions(instanceExtensionCount);
		vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, instanceExtensions.data());

		std::vector<const char*> activeInstanceExtensions;

		uint32_t glfwRequiredCount;
		auto glfwRequiredExtensionNames = glfwGetRequiredInstanceExtensions(&glfwRequiredCount);
		std::for_each_n(glfwRequiredExtensionNames, glfwRequiredCount, [&](auto v) { activeInstanceExtensions.push_back(v); });

		// VK_EXT_full_screen_exclusiveのために依存する拡張機能を有効化する.
		auto instanceExtensionRequired = {
			VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
			VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
			// ----
#ifdef _DEBUG
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME
#endif
		};
		for (auto v : instanceExtensionRequired)
		{
			activeInstanceExtensions.push_back(v);
		}
		
		std::vector<const char*> activeInstanceLayers = {
#ifdef _DEBUG
			"VK_LAYER_KHRONOS_validation"
#endif
		};

		VkApplicationInfo appInfo{
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.pApplicationName = "Fullscreen Sample",
			.pEngineName = "Sample",
			.apiVersion = VK_MAKE_VERSION(1, 0, 0)
		};

		VkInstanceCreateInfo instanceCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pApplicationInfo = &appInfo,
			.enabledLayerCount = uint32_t(activeInstanceLayers.size()),
			.ppEnabledLayerNames = (activeInstanceLayers.size() > 0) ? activeInstanceLayers.data() : nullptr,
			.enabledExtensionCount = uint32_t(activeInstanceExtensions.size()),
			.ppEnabledExtensionNames = (activeInstanceExtensions.size()>0) ? activeInstanceExtensions.data() : nullptr,
		};

#ifdef _DEBUG
		VkDebugUtilsMessengerCreateInfoEXT debugUtilsCreateInfo{
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
			.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
			.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
			.pfnUserCallback = VulkanDebugCallback,
		};
		instanceCreateInfo.pNext = &debugUtilsCreateInfo;
#endif
		auto res = vkCreateInstance(&instanceCreateInfo, nullptr, &m_vkInstance);
		if (res != VK_SUCCESS)
		{
			OutputDebugStringA("Failed vkCreateInstance().\n");
			return false;
		}
		volkLoadInstance(m_vkInstance);
#ifdef _DEBUG
		res = vkCreateDebugUtilsMessengerEXT(m_vkInstance, &debugUtilsCreateInfo, nullptr, &m_debugUtils);
		if (res != VK_SUCCESS)
		{
			OutputDebugStringA("Failed vkCreateDebugUtilsMessengerEXT().\n");
			return false;
		}
#endif

		return true;
	}

	bool InitializeVulkanDevice()
	{
		uint32_t gpucount;
		vkEnumeratePhysicalDevices(m_vkInstance, &gpucount, nullptr);
		if (gpucount == 0)
		{
			return false;
		}
		std::vector<VkPhysicalDevice> gpus(gpucount);
		vkEnumeratePhysicalDevices(m_vkInstance, &gpucount, gpus.data());

		// 最初のGPUデバイスを使用する.
		m_gpu = gpus[0];

		uint32_t familyPropsCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(m_gpu, &familyPropsCount, nullptr);
		std::vector<VkQueueFamilyProperties> familyProps(familyPropsCount);
		vkGetPhysicalDeviceQueueFamilyProperties(m_gpu, &familyPropsCount, familyProps.data());

		for (int i = 0; auto & props : familyProps)
		{
			VkBool32 supports_present;
			vkGetPhysicalDeviceSurfaceSupportKHR(m_gpu, i, m_surface, &supports_present);

			if (props.queueFlags & VK_QUEUE_GRAPHICS_BIT)
			{
				m_graphicsQueueIndex = i;
				break;
			}
			++i;
		}

		uint32_t formatCount;
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_gpu, m_surface, &formatCount, nullptr);
		std::vector<VkSurfaceFormatKHR> formats(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_gpu, m_surface, &formatCount, formats.data());
		
		for (auto& f : formats)
		{
			if (f.format == VK_FORMAT_B8G8R8A8_UNORM)
			{
				m_swapchainContext.format = f.format;
				break;
			}
		}
		if (m_swapchainContext.format == VK_FORMAT_UNDEFINED)
		{
			for (auto& f : formats)
			{
				if (f.format == VK_FORMAT_R8G8B8A8_UNORM)
				{
					m_swapchainContext.format = f.format;
					break;
				}
			}
		}

		std::vector<const char*> activeDeviceExtensions = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME,
		};

		float defaultPrior = 1.0f;
		VkDeviceQueueCreateInfo queueCreateInfo{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = m_graphicsQueueIndex,
			.queueCount = 1,
			.pQueuePriorities = &defaultPrior,
		};
		VkDeviceCreateInfo deviceCreateInfo{
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &queueCreateInfo,
			.enabledExtensionCount = uint32_t(activeDeviceExtensions.size()),
			.ppEnabledExtensionNames = (activeDeviceExtensions.size() > 0) ? activeDeviceExtensions.data() : nullptr,
		};
		auto res = vkCreateDevice(m_gpu, &deviceCreateInfo, nullptr, &m_vkDevice);
		if (res != VK_SUCCESS)
		{
			OutputDebugStringA("vkCreateDevice failed.\n");
			return false;
		}
		volkLoadDevice(m_vkDevice);
		vkGetDeviceQueue(m_vkDevice, m_graphicsQueueIndex, 0, &m_deviceQueue);
		return true;
	}

	void InitializeSwapchain()
	{
		VkSurfaceCapabilitiesKHR surfaceCaps{};
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_gpu, m_surface, &surfaceCaps);
		
		VkExtent2D swapchainSize;

		if (surfaceCaps.currentExtent.width == 0xFFFFFFFFu)
		{
			swapchainSize = { 1280, 720 };
		}
		else
		{
			swapchainSize = surfaceCaps.currentExtent;
		}

		if (isFullscreen())
		{
			swapchainSize = surfaceCaps.maxImageExtent;
		}

		VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
		uint32_t desiredSwapchainImages = surfaceCaps.minImageCount + 1;
		desiredSwapchainImages = std::min(surfaceCaps.maxImageCount, desiredSwapchainImages);

		VkSwapchainKHR oldSwapchain = m_swapchainContext.swapchain;
		VkSwapchainCreateInfoKHR swapchainCreateInfo{
			.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
			.surface = m_surface,
			.minImageCount = desiredSwapchainImages,
			.imageFormat = m_swapchainContext.format,
			.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
			.imageExtent = swapchainSize,
			.imageArrayLayers = 1,
			.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
			.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			.presentMode = presentMode,
			.clipped = VK_TRUE,
			.oldSwapchain = oldSwapchain,
		};

		swapchainCreateInfo.pNext = &m_swapchainContext.surfaceFullScreenExclusiveInfo;

		auto res = vkCreateSwapchainKHR(m_vkDevice, &swapchainCreateInfo, nullptr, &m_swapchainContext.swapchain);
		if (res == VK_ERROR_INITIALIZATION_FAILED)
		{
			return;
		}

		if (oldSwapchain != VK_NULL_HANDLE)
		{
			for (auto imageView : m_swapchainContext.imageViews)
			{
				vkDestroyImageView(m_vkDevice, imageView, nullptr);
			}
			uint32_t imageCount;
			vkGetSwapchainImagesKHR(m_vkDevice, oldSwapchain, &imageCount, nullptr);
			for (size_t i = 0; i < imageCount; ++i)
			{
				TeardownPerFrame(m_frames[i]);
			}
			m_swapchainContext.imageViews.clear();
			vkDestroySwapchainKHR(m_vkDevice, oldSwapchain, nullptr);
		}
		m_swapchainContext.dimensions = swapchainSize;
		uint32_t imageCount;
		vkGetSwapchainImagesKHR(m_vkDevice, m_swapchainContext.swapchain, &imageCount, nullptr);
		std::vector<VkImage> swapchainImages(imageCount);
		vkGetSwapchainImagesKHR(m_vkDevice, m_swapchainContext.swapchain, &imageCount, swapchainImages.data());

		m_frames.clear();
		m_frames.resize(imageCount);
		for (size_t i = 0; i < imageCount; ++i)
		{
			InitPerFrame(m_frames[i]);
		}
		for (size_t i = 0; i < imageCount; ++i)
		{
			VkImageViewCreateInfo viewCreateInfo{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = swapchainImages[i],
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = m_swapchainContext.format,
				.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A },
				.subresourceRange = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1
				}
			};
			VkImageView view;
			vkCreateImageView(m_vkDevice, &viewCreateInfo, nullptr, &view);
			m_swapchainContext.imageViews.push_back(view);
		}
	}


	void InitializeRenderPass()
	{
		VkAttachmentDescription attachment = { 0 };
		attachment.format = m_swapchainContext.format;
		attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpass = { 0 };
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;

		VkSubpassDependency dependency = { 
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = 0,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
		};

		VkRenderPassCreateInfo rp_info = { 
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = 1,
			.pAttachments = &attachment,
			.subpassCount = 1,
			.pSubpasses = &subpass,
			.dependencyCount = 1,
			.pDependencies = &dependency
		};

		vkCreateRenderPass(m_vkDevice, &rp_info, nullptr, &m_renderPass);
	}
	VkShaderModule CreateShaderModule(const uint32_t* data, size_t length)
	{
		VkShaderModuleCreateInfo moduleCreateInfo{
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = length,
			.pCode = data
		};
		VkShaderModule shaderModule;
		vkCreateShaderModule(m_vkDevice, &moduleCreateInfo, nullptr, &shaderModule);
		return shaderModule;
	}

	void InitializePipeline()
	{
		VkPipelineLayoutCreateInfo layoutInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
		};
		vkCreatePipelineLayout(m_vkDevice, &layoutInfo, nullptr, &m_pipelineLayout);

		VkPipelineVertexInputStateCreateInfo vertexInput{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
		};

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
		};
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineRasterizationStateCreateInfo raster{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.cullMode = VK_CULL_MODE_BACK_BIT,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
			.lineWidth = 1.0f,
		};

		VkPipelineColorBlendAttachmentState blendAttachment{
			.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
		};

		VkPipelineColorBlendStateCreateInfo blend{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.attachmentCount = 1,
			.pAttachments = &blendAttachment
		};

		VkPipelineViewportStateCreateInfo viewport{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.viewportCount = 1,
			.scissorCount = 1,
		};

		VkPipelineDepthStencilStateCreateInfo depthStencil{
			.stencilTestEnable = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
		};

		VkPipelineMultisampleStateCreateInfo multisample{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		};

		std::array<VkDynamicState, 2> dynamics{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

		VkPipelineDynamicStateCreateInfo dynamic{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = uint32_t(dynamics.size()),
			.pDynamicStates = dynamics.data(),
		};

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{{
			{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_VERTEX_BIT,
				.module= CreateShaderModule(gVS, sizeof(gVS)),
				.pName = "main",
			},
			{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
				.module = CreateShaderModule(gFS, sizeof(gFS)),
				.pName = "main",
			}
		}};

		VkGraphicsPipelineCreateInfo pipelineCreateInfo{
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.stageCount = uint32_t(shaderStages.size()),
			.pStages = shaderStages.data(),
			.pVertexInputState = &vertexInput,
			.pInputAssemblyState = &inputAssembly,
			.pViewportState = &viewport,
			.pRasterizationState = &raster,
			.pMultisampleState = &multisample,
			.pDepthStencilState = &depthStencil,
			.pColorBlendState = &blend,
			.pDynamicState = &dynamic,
			.layout = m_pipelineLayout,
			.renderPass = m_renderPass,
		};

		vkCreateGraphicsPipelines(m_vkDevice, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_pipeline);

		for (auto& m : shaderStages)
		{
			vkDestroyShaderModule(m_vkDevice, m.module, nullptr);
		}
	}
	void InitializeFramebuffers()
	{
		for (auto& view : m_swapchainContext.imageViews)
		{
			VkFramebufferCreateInfo framebufferCreateInfo{
				.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
				.renderPass = m_renderPass,
				.attachmentCount = 1,
				.pAttachments = &view,
				.width = m_swapchainContext.dimensions.width,
				.height = m_swapchainContext.dimensions.height,
				.layers = 1,
			};
			VkFramebuffer fb;
			vkCreateFramebuffer(m_vkDevice, &framebufferCreateInfo, nullptr, &fb);
			m_swapchainContext.framebuffers.push_back(fb);
		}
	}

	VkResult AcquireNextImage(uint32_t* imageIndex)
	{
		vkAcquireNextImageKHR(m_vkDevice, m_swapchainContext.swapchain, UINT64_MAX, m_semPresentComplete, VK_NULL_HANDLE, imageIndex);

		auto& frame = m_frames[*imageIndex];
		if (frame.queueSubmitFence != VK_NULL_HANDLE)
		{
			vkWaitForFences(m_vkDevice, 1, &frame.queueSubmitFence, VK_TRUE, UINT64_MAX);
			vkResetFences(m_vkDevice, 1, &frame.queueSubmitFence);
		}

		if (frame.commandPool != VK_NULL_HANDLE)
		{
			vkResetCommandPool(m_vkDevice, frame.commandPool, 0);
		}

		return VK_SUCCESS;
	}
	VkResult PresentImage(uint32_t index)
	{
		VkPresentInfoKHR present{
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &m_semRenderComplete,
			.swapchainCount = 1,
			.pSwapchains = &m_swapchainContext.swapchain,
			.pImageIndices = &index,
		};

		return vkQueuePresentKHR(m_deviceQueue, &present);
	}

	void UpdateApplicationWindow()
	{
		auto hwnd = glfwGetWin32Window(m_window);

		if (m_mode == Windowed)
		{
			SetWindowLongA(hwnd, GWL_STYLE, m_windowStyle);
			SetWindowLongA(hwnd, GWL_EXSTYLE, m_windowStyleEx);
			ShowWindow(hwnd, SW_SHOWNORMAL);
			SetWindowPlacement(hwnd, &m_wpc);
		}
		if (m_mode == BorderlessFullscreen || m_mode == ExclusiveFullscreen)
		{
			LONG newStyle = m_windowStyle & (~WS_BORDER) & (~WS_DLGFRAME) & (~WS_THICKFRAME);
			LONG newStyleEx = m_windowStyleEx & (~WS_EX_WINDOWEDGE);
			newStyle |= WS_POPUP;
			newStyleEx |= WS_EX_TOPMOST;
			SetWindowLongA(hwnd, GWL_STYLE, newStyle);
			SetWindowLongA(hwnd, GWL_EXSTYLE, newStyleEx);
			ShowWindow(hwnd, SW_SHOWMAXIMIZED);
		}
	}

	void Resize(int width, int height)
	{
		if (m_vkDevice == VK_NULL_HANDLE)
		{
			return;
		}
		VkSurfaceCapabilitiesKHR surfaceCaps;
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_gpu, m_surface, &surfaceCaps);

		if (surfaceCaps.currentExtent.width == m_swapchainContext.dimensions.width &&
			surfaceCaps.currentExtent.height == m_swapchainContext.dimensions.height)
		{
			return;
		}
		vkDeviceWaitIdle(m_vkDevice);
		TeardownFramebuffers();

		InitializeSwapchain();
		InitializeFramebuffers();
	}

	void RecreateSwapchain()
	{
		if (m_vkDevice == VK_NULL_HANDLE)
		{
			return;
		}
		vkDeviceWaitIdle(m_vkDevice);
		TeardownFramebuffers();

		InitializeSwapchain();
		InitializeFramebuffers();

		UpdateApplicationWindow();

		if ( isExclusiveFullscreen() )
		{
			auto res = vkAcquireFullScreenExclusiveModeEXT(m_vkDevice, m_swapchainContext.swapchain);
			if (res != VK_SUCCESS)
			{
				auto str = std::format("vkAcquireFullScreenExclusiveModeEXT failed. (result = {:d})\n", (int)res);
				OutputDebugStringA(str.c_str());
			}
		}

	}

	static VkBool32 VKAPI_CALL VulkanDebugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* data,
		void* userData )
	{
		if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		{
			OutputDebugStringA(data->pMessage);
		}
		return VK_FALSE;
	}

	void OnKeyDown(int key, int mods)
	{

		if (key == GLFW_KEY_ESCAPE)
		{
			glfwSetWindowShouldClose(m_window, GLFW_TRUE);
			return;
		}
		else if (key == GLFW_KEY_F1)
		{
			EnterWindowMode();
		}
		else if (key == GLFW_KEY_F2)
		{
			EnterBorderlessFullscreen();
		}
		else if (key == GLFW_KEY_F3)
		{
			EnterExclusiveFullscreen();
		}

	}
	bool isFullscreen() const
	{
		return m_mode != Windowed;
	}
	bool isExclusiveFullscreen() const
	{
		return m_swapchainContext.surfaceFullScreenExclusiveInfo.fullScreenExclusive == VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT;
	}
private:
	struct FrameInfo
	{
		VkCommandPool commandPool = VK_NULL_HANDLE;
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		VkFence queueSubmitFence = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;
		uint32_t queueIndex = 0;
	};
	struct SwapchainContext
	{
		VkExtent2D dimensions{};
		VkFormat   format = VK_FORMAT_UNDEFINED;
		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		std::vector<VkImageView> imageViews;
		std::vector<VkFramebuffer> framebuffers;

		VkSurfaceFullScreenExclusiveInfoEXT surfaceFullScreenExclusiveInfo = {};
		VkSurfaceFullScreenExclusiveWin32InfoEXT surfaceFullScreenExclusiveWin32Info = {};
	};

	GLFWwindow* m_window = nullptr;
	VkInstance m_vkInstance = VK_NULL_HANDLE;
	VkDevice   m_vkDevice = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT m_debugUtils = VK_NULL_HANDLE;
	VkPhysicalDevice m_gpu = VK_NULL_HANDLE;
	uint32_t m_graphicsQueueIndex = 0;
	VkQueue m_deviceQueue = VK_NULL_HANDLE;
	VkSurfaceKHR m_surface = VK_NULL_HANDLE;
	SwapchainContext m_swapchainContext;
	std::vector<FrameInfo>   m_frames{};
	VkSemaphore m_semRenderComplete = VK_NULL_HANDLE;
	VkSemaphore m_semPresentComplete = VK_NULL_HANDLE;
	VkRenderPass m_renderPass = VK_NULL_HANDLE;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_pipeline = VK_NULL_HANDLE;

	enum Mode {
		Windowed,
		BorderlessFullscreen,
		ExclusiveFullscreen,
	};
	Mode m_mode = Windowed;
	LONG m_windowStyle = 0;
	LONG m_windowStyleEx = 0;
	WINDOWPLACEMENT m_wpc;

	void InitPerFrame(FrameInfo& frameInfo)
	{
		VkFenceCreateInfo fenceCreateInfo{
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT,
		};
		vkCreateFence(m_vkDevice, &fenceCreateInfo, nullptr, &frameInfo.queueSubmitFence);

		VkCommandPoolCreateInfo commandPoolCreateInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
			.queueFamilyIndex = m_graphicsQueueIndex,
		};
		vkCreateCommandPool(m_vkDevice, &commandPoolCreateInfo, nullptr, &frameInfo.commandPool);

		VkCommandBufferAllocateInfo commandBufferAllocateInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = frameInfo.commandPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};
		vkAllocateCommandBuffers(m_vkDevice, &commandBufferAllocateInfo, &frameInfo.commandBuffer);
		frameInfo.device = m_vkDevice;
		frameInfo.queueIndex = m_graphicsQueueIndex;
	}
	void TeardownPerFrame(FrameInfo& frameInfo)
	{
		if (frameInfo.queueSubmitFence != VK_NULL_HANDLE)
		{
			vkDestroyFence(m_vkDevice, frameInfo.queueSubmitFence, nullptr);
			frameInfo.queueSubmitFence = VK_NULL_HANDLE;
		}
		if (frameInfo.commandBuffer != VK_NULL_HANDLE)
		{
			vkFreeCommandBuffers(m_vkDevice, frameInfo.commandPool, 1, &frameInfo.commandBuffer);
			frameInfo.commandBuffer = VK_NULL_HANDLE;
		}
		if (frameInfo.commandPool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(m_vkDevice, frameInfo.commandPool, nullptr);
			frameInfo.commandPool = VK_NULL_HANDLE;
		}

		frameInfo.device = VK_NULL_HANDLE;
		frameInfo.queueIndex = 0;
	}

	void TeardownFramebuffers()
	{
		vkQueueWaitIdle(m_deviceQueue);
		for (auto& fb : m_swapchainContext.framebuffers)
		{
			vkDestroyFramebuffer(m_vkDevice, fb, nullptr);
		}
		m_swapchainContext.framebuffers.clear();
	}

	void EnterWindowMode()
	{
		if (m_mode == Windowed)
		{
			return;
		}

		if (isExclusiveFullscreen())
		{
			// 前回のセットを解除する.
			vkReleaseFullScreenExclusiveModeEXT(m_vkDevice, m_swapchainContext.swapchain);
		}

		m_mode = Windowed;
		m_swapchainContext.surfaceFullScreenExclusiveInfo.fullScreenExclusive = VK_FULL_SCREEN_EXCLUSIVE_DISALLOWED_EXT;
		RecreateSwapchain();
	}
	void EnterBorderlessFullscreen()
	{
		if (m_mode == BorderlessFullscreen)
		{
			return;
		}
		if (isExclusiveFullscreen())
		{
			// 前回のセットを解除する.
			vkReleaseFullScreenExclusiveModeEXT(m_vkDevice, m_swapchainContext.swapchain);
		}

		m_mode = BorderlessFullscreen;
		m_swapchainContext.surfaceFullScreenExclusiveInfo.fullScreenExclusive = VK_FULL_SCREEN_EXCLUSIVE_ALLOWED_EXT;

		RecreateSwapchain();
	}
	void EnterExclusiveFullscreen()
	{
		if (m_mode == ExclusiveFullscreen)
		{
			return;
		}
		m_mode = ExclusiveFullscreen;
		m_swapchainContext.surfaceFullScreenExclusiveInfo.fullScreenExclusive = VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT;
		RecreateSwapchain();
	}
};

void FullscreenExclusiveApp::KeyProcessCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	auto* app = static_cast<FullscreenExclusiveApp*>(glfwGetWindowUserPointer(window));
	if (app != nullptr)
	{
		if (action == GLFW_PRESS)
		{
			app->OnKeyDown(key, mods);
		}
	}
}

void FullscreenExclusiveApp::WindowSizeCallback(GLFWwindow* window, int width, int height)
{
	auto* app = static_cast<FullscreenExclusiveApp*>(glfwGetWindowUserPointer(window));
	if (app != nullptr)
	{
		app->Resize(width, height);
	}
}


int __stdcall wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nCmdShow)
{

	FullscreenExclusiveApp app;
	if (app.Initialize())
	{
		app.Run();
		app.Shutdown();
	}
	else
	{
		return -1;
	}

	return 0;
}

