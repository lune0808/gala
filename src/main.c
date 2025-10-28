#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <inttypes.h>
#include <stdint.h>
#include <assert.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>


#define ARRAY_SIZE(a) (sizeof (a) / sizeof *(a))

typedef uint32_t u32;

noreturn void crash(const char *reason, ...)
{
	va_list args;
	va_start(args, reason);
	vfprintf(stderr, reason, args);
	fputs("", stderr);
	va_end(args);
	exit(1);

}

void *xmalloc(size_t sz)
{
	void *ptr = malloc(sz);
	if (!ptr) crash("malloc");
	return ptr;
}

void init_glfw_or_crash()
{
	if (glfwInit() != GLFW_TRUE) crash("glfwInit");
}

GLFWwindow *init_window_or_crash(int width, int height, const char *title)
{
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow *win = glfwCreateWindow(width, height, title, NULL, NULL);
	if (!win) crash("glfwCreateWindow");
	return win;
}

static const char *const validation = "VK_LAYER_KHRONOS_validation";

void vulkan_validation_layers_or_crash()
{
	u32 n_lyr;
	vkEnumerateInstanceLayerProperties(&n_lyr, NULL);
	VkLayerProperties *lyr = xmalloc(n_lyr * sizeof *lyr);
	vkEnumerateInstanceLayerProperties(&n_lyr, lyr);
	for (VkLayerProperties *cur = lyr; cur != lyr + n_lyr; cur++) {
		if (strcmp(validation, cur->layerName) == 0) {
			return;
		}
	}
	free(lyr);
	crash("validation layer '%s' not found");
}

VkInstance vulkan_instance_or_crash()
{
	VkApplicationInfo app_desc = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "Gala",
		.applicationVersion = VK_MAKE_VERSION(0, 0, 0),
		.pEngineName = "No engine",
		.engineVersion = VK_MAKE_VERSION(0, 0, 0),
		.apiVersion = VK_API_VERSION_1_0,
	};
	VkInstanceCreateInfo inst_desc = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app_desc,
	};
#ifndef NDEBUG
	vulkan_validation_layers_or_crash();
	inst_desc.enabledLayerCount = 1;
	inst_desc.ppEnabledLayerNames = &validation;
#endif
	inst_desc.ppEnabledExtensionNames = glfwGetRequiredInstanceExtensions(&inst_desc.enabledExtensionCount);
	VkInstance inst;
	VkResult status = vkCreateInstance(&inst_desc, NULL, &inst);
	if (status != VK_SUCCESS) crash("vkCreateInstance");
	return inst;
}

typedef struct {
	union {
		struct {
			u32 graphics;
			u32 present;
		};
		u32 array_repr[0];
	};

	u32 missing;
} queue_families;

queue_families required_queue_families(VkPhysicalDevice dev, VkSurfaceKHR surface)
{
	queue_families req;
	req.missing = 1u | 2u;
	u32 n_qf;
	vkGetPhysicalDeviceQueueFamilyProperties(dev, &n_qf, NULL);
	VkQueueFamilyProperties *qf = xmalloc(n_qf * sizeof *qf);
	vkGetPhysicalDeviceQueueFamilyProperties(dev, &n_qf, qf);
	for (u32 i = 0; i < n_qf; i++) {
		if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			req.graphics = i;
			req.missing &= ~1u;
		}
		VkBool32 presentable;
		vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &presentable);
		if (presentable) {
			req.present = i;
			req.missing &= ~2u;
		}
	}
	free(qf);
	return req;
}

static inline u32 min_u32(u32 a, u32 b)
{
	return (a < b)? a: b;
}

static inline u32 max_u32(u32 a, u32 b)
{
	return (a < b)? b: a;
}

static inline u32 clamp_u32(u32 x, u32 lo, u32 hi)
{
	return max_u32(lo, min_u32(x, hi));
}

VkExtent2D swapchain_select_resolution(VkPhysicalDevice dev, VkSurfaceKHR surface, GLFWwindow *win, VkSurfaceCapabilitiesKHR *pcap)
{
	VkSurfaceCapabilitiesKHR cap;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, surface, &cap);
	*pcap = cap;
	if (cap.currentExtent.width == UINT32_MAX) {
		int width, height;
		glfwGetFramebufferSize(win, &width, &height);
		return (VkExtent2D){
			clamp_u32((u32) width, cap.minImageExtent.width, cap.maxImageExtent.height),
			clamp_u32((u32) height, cap.minImageExtent.height, cap.maxImageExtent.height),
		};
	} else {
		return cap.currentExtent;
	}
}

VkSurfaceFormatKHR swapchain_select_pixels(VkPhysicalDevice dev, VkSurfaceKHR surface)
{
	u32 n_fmt;
	vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &n_fmt, NULL);
	VkSurfaceFormatKHR *fmt = xmalloc(n_fmt * sizeof *fmt);
	vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &n_fmt, fmt);
	VkSurfaceFormatKHR selected = fmt[0];
	for (u32 i = 0; i < n_fmt; i++) {
		if (fmt[i].format == VK_FORMAT_B8G8R8A8_SRGB && fmt[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			selected = fmt[i];
			break;
		}
	}
	free(fmt);
	return selected;
}

VkPresentModeKHR swapchain_select_latency(VkPhysicalDevice dev, VkSurfaceKHR surface)
{
	u32 n_swap;
	vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &n_swap, NULL);
	VkPresentModeKHR *swap = xmalloc(n_swap * sizeof *swap);
	vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &n_swap, swap);
	VkPresentModeKHR selected = VK_PRESENT_MODE_FIFO_KHR;
	for (u32 i = 0; i < n_swap; i++) {
		if (swap[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
			selected = swap[i];
			break;
		}
	}
	free(swap);
	return selected;
}

VkImageView swapchain_view(VkDevice logical, VkImage img, VkFormat fmt)
{
	VkImageViewCreateInfo view_desc = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = img,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = fmt,
		.components.r = VK_COMPONENT_SWIZZLE_IDENTITY,
		.components.g = VK_COMPONENT_SWIZZLE_IDENTITY,
		.components.b = VK_COMPONENT_SWIZZLE_IDENTITY,
		.components.a = VK_COMPONENT_SWIZZLE_IDENTITY,
		.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.subresourceRange.baseMipLevel = 0,
		.subresourceRange.levelCount = 1,
		.subresourceRange.baseArrayLayer = 0,
		.subresourceRange.layerCount = 1,
	};
	VkImageView view;
	if (vkCreateImageView(logical, &view_desc, NULL, &view) != VK_SUCCESS)
		crash("vkCreateImageView");
	return view;
}

VkRenderPass render_pass_create_or_crash(VkDevice logical, VkFormat fmt)
{
	VkAttachmentDescription color_attacht = {
		.format = fmt,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	};
	VkAttachmentReference color_attacht_ref = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	VkSubpassDescription subpass_desc = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &color_attacht_ref,
	};
	VkSubpassDependency draw_dep = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstSubpass = 0,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	};
	VkRenderPassCreateInfo pass_desc = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &color_attacht,
		.subpassCount = 1,
		.pSubpasses = &subpass_desc,
		.dependencyCount = 1,
		.pDependencies = &draw_dep,
	};
	VkRenderPass pass;
	if (vkCreateRenderPass(logical, &pass_desc, NULL, &pass) != VK_SUCCESS)
		crash("vkCreateRenderPass");
	return pass;
}

typedef struct {
	VkSwapchainKHR chain;
	VkImage *slot;
	VkImageView *view;
	u32 n_slot;
	u32 current;
	VkFormat fmt;
	VkExtent2D dim;
} swapchain;

swapchain swapchain_create(VkDevice logical, VkSurfaceKHR surface, GLFWwindow *win, queue_families *fam, VkPhysicalDevice dev)
{
	VkSurfaceCapabilitiesKHR cap;
	VkExtent2D dim = swapchain_select_resolution(dev, surface, win, &cap);
	VkPresentModeKHR mode = swapchain_select_latency(dev, surface);
	VkSurfaceFormatKHR fmt = swapchain_select_pixels(dev, surface);
	u32 expected_swap_cnt = cap.minImageCount + 1u;
	VkSwapchainCreateInfoKHR swap_desc = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = (cap.maxImageCount != 0)? min_u32(cap.maxImageCount, expected_swap_cnt): expected_swap_cnt,
		.imageFormat = fmt.format,
		.imageColorSpace = fmt.colorSpace,
		.imageExtent = dim,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = cap.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = mode,
		.clipped = VK_TRUE,
		.oldSwapchain = VK_NULL_HANDLE,
	};
	if (fam->graphics == fam->present) {
		swap_desc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	} else {
		swap_desc.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		swap_desc.queueFamilyIndexCount = 2;
		swap_desc.pQueueFamilyIndices = fam->array_repr;
	}
	swapchain swap;
	if (vkCreateSwapchainKHR(logical, &swap_desc, NULL, &swap.chain) != VK_SUCCESS)
		crash("vkCreateSwapchainKHR");
	vkGetSwapchainImagesKHR(logical, swap.chain, &swap.n_slot, NULL);
	swap.slot = xmalloc(swap.n_slot * sizeof *swap.slot);
	vkGetSwapchainImagesKHR(logical, swap.chain, &swap.n_slot, swap.slot);
	swap.fmt = fmt.format;
	swap.dim = dim;
	swap.view = xmalloc(swap.n_slot * sizeof *swap.view);
	for (u32 i = 0; i < swap.n_slot; i++) {
		swap.view[i] = swapchain_view(logical, swap.slot[i], fmt.format);
	}
	return swap;
}

VkFramebuffer *framebuf_attach_or_crash(VkDevice logical, swapchain swap, VkRenderPass pass)
{
	VkFramebuffer *framebuf = xmalloc(swap.n_slot * sizeof *framebuf);
	for (u32 i = 0; i < swap.n_slot; i++) {
		VkFramebufferCreateInfo framebuf_desc = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = pass,
			.attachmentCount = 1,
			.pAttachments = &swap.view[i],
			.width = swap.dim.width,
			.height = swap.dim.height,
			.layers = 1,
		};
		if (vkCreateFramebuffer(logical, &framebuf_desc, NULL, &framebuf[i]) != VK_SUCCESS)
			crash("vkCreateFramebuffer");
	}
	return framebuf;
}

static const char *const swapchain_extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

u32 processor_score(VkPhysicalDevice dev, VkSurfaceKHR surface, queue_families *f)
{
	*f = required_queue_families(dev, surface);
	if (f->missing) {
		return 0;
	}
	u32 n_ext;
	vkEnumerateDeviceExtensionProperties(dev, NULL, &n_ext, NULL);
	VkExtensionProperties *ext = xmalloc(n_ext * sizeof *ext);
	vkEnumerateDeviceExtensionProperties(dev, NULL, &n_ext, ext);
	bool found = false;
	for (u32 i = 0; i < n_ext; i++) {
		if (strcmp(ext[i].extensionName, swapchain_extension) == 0) {
			found = true;
			break;
		}
	}
	free(ext);
	if (!found) {
		return 0;
	}
	VkPhysicalDeviceProperties prop;
	vkGetPhysicalDeviceProperties(dev, &prop);
	VkPhysicalDeviceFeatures feat;
	vkGetPhysicalDeviceFeatures(dev, &feat);
	u32 score = 1;
	if (prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
		score += 0x100;
	} else if (prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
		score += 0x10;
	}
	printf("%*s assigned score of 0x%" PRIx32 ".\n",
		(int) VK_MAX_PHYSICAL_DEVICE_NAME_SIZE, prop.deviceName, score);
	return score;
}

VkPhysicalDevice vulkan_select_gpu_or_crash(VkInstance inst, VkSurfaceKHR surface, queue_families *queues)
{
	VkPhysicalDevice selected = VK_NULL_HANDLE;
	u32 n_gpu;
	vkEnumeratePhysicalDevices(inst, &n_gpu, NULL);
	VkPhysicalDevice *gpu = xmalloc(n_gpu * sizeof *gpu);
	vkEnumeratePhysicalDevices(inst, &n_gpu, gpu);
	u32 best_score = 0;
	for (u32 i = 0; i < n_gpu; i++) {
		queue_families cur_queues;
		u32 score = processor_score(gpu[i], surface, &cur_queues);
		if (score > best_score) {
			best_score = score;
			selected = gpu[i];
			*queues = cur_queues;
		}
	}
	free(gpu);
	if (selected == VK_NULL_HANDLE)
		crash("suitable processor not found.");
	return selected;
}

typedef struct {
	VkDevice logical;
	VkQueue graphics;
	VkQueue present;
} logical_interface;

logical_interface vulkan_logical_device_or_crash(VkPhysicalDevice physical, queue_families *queues)
{
	float prio = 1.0f;
	VkDeviceQueueCreateInfo queue_desc[] = {
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = queues->graphics,
			.queueCount = 1,
			.pQueuePriorities = &prio,
		},
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = queues->present,
			.queueCount = 1,
			.pQueuePriorities = &prio,
		},
	};
	VkPhysicalDeviceFeatures feat = {};
	bool sep_queues = (queues->present == queues->graphics);
	VkDeviceCreateInfo logdev_desc = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pQueueCreateInfos = queue_desc,
		.queueCreateInfoCount = sep_queues? 1: 2,
		.pEnabledFeatures = &feat,
		.enabledExtensionCount = 1,
		.ppEnabledExtensionNames = &swapchain_extension,
		.enabledLayerCount = 1,
		.ppEnabledLayerNames = &validation,
	};
	logical_interface i;
	if (vkCreateDevice(physical, &logdev_desc, NULL, &i.logical) != VK_SUCCESS)
		crash("vkCreateDevice");
	vkGetDeviceQueue(i.logical, queues->graphics, 0, &i.graphics);
	vkGetDeviceQueue(i.logical, queues->present, !sep_queues, &i.present);
	return i;
}

VkSurfaceKHR window_surface(VkInstance inst, GLFWwindow *win)
{
	VkSurfaceKHR surface;
	if (glfwCreateWindowSurface(inst, win, NULL, &surface) != VK_SUCCESS)
		crash("couldn't create a surface to present to");
	return surface;
}

typedef struct {
	void *mem;
	size_t size;
} buffer;

buffer load_file_or_crash(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) goto fail_open;
	long status;
	status = fseek(f, 0, SEEK_END);
	if (status != 0) goto io;
	status = ftell(f);
	if (status < 0) goto io;
	size_t sz = (size_t) status;
	status = fseek(f, 0, SEEK_SET);
	if (status != 0) goto io;
	void *buf = malloc(sz);
	if (!buf) goto io;
	size_t remaining_sz = sz;
	for (void *cur = buf; !feof(f);) {
		size_t amount_read = fread(cur, remaining_sz, 1, f);
		cur += amount_read;
		remaining_sz -= amount_read;
		if (ferror(f)) goto alloc;
	}
	fclose(f);
	return (buffer){ buf, sz };
alloc:
	free(buf);
io:
	fclose(f);
fail_open:
	crash("load file '%s' failed");
}

VkShaderModule build_shader_module_or_crash(const char *path, VkDevice logical)
{
	buffer buf = load_file_or_crash(path);
	VkShaderModuleCreateInfo desc = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = buf.size,
		.pCode = buf.mem,
	};
	VkShaderModule sh;
	if (vkCreateShaderModule(logical, &desc, NULL, &sh) != VK_SUCCESS)
		crash("build shader %s failed", path);
	free(buf.mem);
	return sh;
}

typedef struct {
	VkPipeline line;
	VkPipelineLayout layout;
} pipeline;

pipeline graphics_pipeline_create_or_crash(const char *vert_path, const char *frag_path, VkDevice logical, VkExtent2D dims, VkRenderPass gpass)
{
	VkShaderModule vert_mod = build_shader_module_or_crash(vert_path, logical);
	VkShaderModule frag_mod = build_shader_module_or_crash(frag_path, logical);
	VkPipelineShaderStageCreateInfo stg_desc[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vert_mod,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = frag_mod,
			.pName = "main",
		}
	};
	VkPipelineDynamicStateCreateInfo dyn_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
	};
	VkPipelineVertexInputStateCreateInfo vert_lyt_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	};
	VkPipelineInputAssemblyStateCreateInfo ia_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE,
	};
	VkPipelineViewportStateCreateInfo vp_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = &(VkViewport){
			.x = 0.0f,
			.y = 0.0f,
			.width = (float) dims.width,
			.height = (float) dims.height,
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		},
		.scissorCount = 1,
		.pScissors = &(VkRect2D){
			.offset = {0, 0},
			.extent = dims,
		},
	};
	VkPipelineRasterizationStateCreateInfo ras_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.lineWidth = 1.0f,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable = VK_FALSE,
	};
	VkPipelineMultisampleStateCreateInfo ms_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.sampleShadingEnable = VK_FALSE,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};
	VkPipelineColorBlendAttachmentState blend_attach = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
				| VK_COLOR_COMPONENT_G_BIT
				| VK_COLOR_COMPONENT_B_BIT
				| VK_COLOR_COMPONENT_A_BIT,
		.blendEnable = VK_FALSE,
	};
	VkPipelineColorBlendStateCreateInfo blend_global = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = VK_FALSE,
		.attachmentCount = 1,
		.pAttachments = &blend_attach,
	};
	VkPipelineLayoutCreateInfo unif_lyt_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
	};
	VkPipelineLayout unif_lyt;
	if (vkCreatePipelineLayout(logical, &unif_lyt_desc, NULL, &unif_lyt) != VK_SUCCESS)
		crash("vkCreatePipelineLayout");

	VkGraphicsPipelineCreateInfo gpipe_desc = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = ARRAY_SIZE(stg_desc),
		.pStages = stg_desc,
		.pVertexInputState = &vert_lyt_desc,
		.pInputAssemblyState = &ia_desc,
		.pViewportState = &vp_desc,
		.pRasterizationState = &ras_desc,
		.pMultisampleState = &ms_desc,
		.pDepthStencilState = NULL,
		.pColorBlendState = &blend_global,
		.pDynamicState = &dyn_desc,
		.layout = unif_lyt,
		.renderPass = gpass,
		.subpass = 0,
	};
	VkPipeline gpipe;
	if (vkCreateGraphicsPipelines(logical, VK_NULL_HANDLE, 1, &gpipe_desc, NULL, &gpipe) != VK_SUCCESS)
		crash("vkCreateGraphicsPipelines");

	vkDestroyShaderModule(logical, frag_mod, NULL);
	vkDestroyShaderModule(logical, vert_mod, NULL);
	return (pipeline){ gpipe, unif_lyt };
}

VkCommandPool command_pool_create_or_crash(VkDevice logical, u32 queue)
{
	VkCommandPoolCreateInfo pool_desc = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = queue,
	};
	VkCommandPool pool;
	if (vkCreateCommandPool(logical, &pool_desc, NULL, &pool) != VK_SUCCESS)
		crash("vkCreateCommandPool");
	return pool;
}

VkCommandBuffer command_buffer_create_or_crash(VkDevice logical, VkCommandPool pool)
{
	VkCommandBufferAllocateInfo buf_desc = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VkCommandBuffer buf;
	if (vkAllocateCommandBuffers(logical, &buf_desc, &buf) != VK_SUCCESS)
		crash("vkCreateCommandBuffers");
	return buf;
}

void command_buffer_record_or_crash(VkCommandBuffer buf, VkRenderPass pass, swapchain swap, pipeline pipe, VkFramebuffer *framebuf)
{
	VkCommandBufferBeginInfo cmd_desc = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
	if (vkBeginCommandBuffer(buf, &cmd_desc) != VK_SUCCESS)
		crash("vkBeginCommandBuffer");
	VkRenderPassBeginInfo pass_desc = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = pass,
		.framebuffer = framebuf[swap.current],
		.renderArea.offset = {0, 0},
		.renderArea.extent = swap.dim,
		.clearValueCount = 1,
		.pClearValues = &(VkClearValue){{{0.0, 0.0, 0.0, 1.0f}}},
	};
	vkCmdBeginRenderPass(buf, &pass_desc, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.line);
	vkCmdDraw(buf, 3, 1, 0, 0);
	vkCmdEndRenderPass(buf);
	if (vkEndCommandBuffer(buf) != VK_SUCCESS)
		crash("vkEndCommandBuffer");
}

typedef struct {
	VkSemaphore present_ready;
	VkSemaphore render_done;
	VkFence rendering;
} fences;

fences sync_create_or_crash(VkDevice logical)
{
	VkSemaphoreCreateInfo sem_desc = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};
	VkFenceCreateInfo fence_desc = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};
	fences sync;
	if (vkCreateSemaphore(logical, &sem_desc, NULL, &sync.present_ready) != VK_SUCCESS)
		crash("vkCreateSemaphore");
	if (vkCreateSemaphore(logical, &sem_desc, NULL, &sync.render_done) != VK_SUCCESS)
		crash("vkCreateSemaphore");
	if (vkCreateFence(logical, &fence_desc, NULL, &sync.rendering) != VK_SUCCESS)
		crash("vkCreateFence");
	return sync;
}

void draw_or_crash(logical_interface interf, fences sync, swapchain swap, VkFramebuffer *framebuf,
	VkCommandBuffer graphics_commands, VkRenderPass graphics_pass, pipeline pipe)
{
	vkWaitForFences(interf.logical, 1, &sync.rendering, VK_TRUE, UINT64_MAX);
	vkResetFences(interf.logical, 1, &sync.rendering);
	vkAcquireNextImageKHR(interf.logical, swap.chain, UINT64_MAX,
		sync.present_ready, VK_NULL_HANDLE, &swap.current);
	vkResetCommandBuffer(graphics_commands, 0);
	command_buffer_record_or_crash(graphics_commands, graphics_pass, swap, pipe, framebuf);
	VkSubmitInfo submission_desc = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &sync.present_ready,
		.pWaitDstStageMask = &(VkPipelineStageFlags){
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		},
		.commandBufferCount = 1,
		.pCommandBuffers = &graphics_commands,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &sync.render_done,
	};
	if (vkQueueSubmit(interf.graphics, 1, &submission_desc, sync.rendering) != VK_SUCCESS)
		crash("vkQueueSubmit");
	VkPresentInfoKHR present_desc = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &sync.render_done,
		.swapchainCount = 1,
		.pSwapchains = &swap.chain,
		.pImageIndices = &swap.current,
	};
	vkQueuePresentKHR(interf.present, &present_desc);
}

static const int WIDTH = 800;
static const int HEIGHT = 600;

int main()
{
	init_glfw_or_crash();
	GLFWwindow *win = init_window_or_crash(WIDTH, HEIGHT, "Gala");
	VkInstance inst = vulkan_instance_or_crash();
	VkSurfaceKHR surface = window_surface(inst, win);
	queue_families queues;
	VkPhysicalDevice physical = vulkan_select_gpu_or_crash(inst, surface, &queues);
	logical_interface interf = vulkan_logical_device_or_crash(physical, &queues);
	swapchain swap = swapchain_create(interf.logical, surface, win, &queues, physical);
	VkRenderPass graphics_pass = render_pass_create_or_crash(interf.logical, swap.fmt);
	VkFramebuffer *framebuf = framebuf_attach_or_crash(interf.logical, swap, graphics_pass);
	pipeline pipe = graphics_pipeline_create_or_crash("bin/shader.vert.spv", "bin/shader.frag.spv", interf.logical, swap.dim, graphics_pass);
	VkCommandPool pool = command_pool_create_or_crash(interf.logical, queues.graphics);
	VkCommandBuffer graphics_commands = command_buffer_create_or_crash(interf.logical, pool);
	fences sync = sync_create_or_crash(interf.logical);
	while (!glfwWindowShouldClose(win)) {
		glfwPollEvents();
		draw_or_crash(interf, sync, swap, framebuf,
			graphics_commands, graphics_pass, pipe);
	}
	vkDeviceWaitIdle(interf.logical);
	vkDestroySemaphore(interf.logical, sync.present_ready, NULL);
	vkDestroySemaphore(interf.logical, sync.render_done, NULL);
	vkDestroyFence(interf.logical, sync.rendering, NULL);
	vkFreeCommandBuffers(interf.logical, pool, 1, &graphics_commands);
	vkDestroyCommandPool(interf.logical, pool, NULL);
	vkDestroyPipeline(interf.logical, pipe.line, NULL);
	vkDestroyPipelineLayout(interf.logical, pipe.layout, NULL);
	for (u32 i = 0; i < swap.n_slot; i++) {
		vkDestroyImageView(interf.logical, swap.view[i], NULL);
		vkDestroyFramebuffer(interf.logical, framebuf[i], NULL);
	}
	vkDestroyRenderPass(interf.logical, graphics_pass, NULL);
	free(swap.view);
	free(swap.slot);
	vkDestroySwapchainKHR(interf.logical, swap.chain, NULL);
	vkDestroyDevice(interf.logical, NULL);
	vkDestroySurfaceKHR(inst, surface, NULL);
	vkDestroyInstance(inst, NULL);
	glfwDestroyWindow(win);
	glfwTerminate();
	return 0;
}

