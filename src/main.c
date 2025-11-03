#include <stdio.h>
#include <math.h>
#include <stddef.h>
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
#include <cglm/cglm.h>
#include <stb/stb_image.h>


#define ARRAY_SIZE(a) (sizeof (a) / sizeof *(a))

#define MAX_FRAMES_RENDERING (2)

typedef uint32_t u32;
typedef uint16_t u16;
typedef float f32;

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
		VkBool32 presentable;
		vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &presentable);
		if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && presentable) {
			req.graphics = i;
			req.present = i;
			req.missing &= ~3u;
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

VkImageView image_view_create(VkDevice logical, VkImage img, VkFormat fmt)
{
	VkImageViewCreateInfo view_desc = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = img,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = fmt,
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
		swap.view[i] = image_view_create(logical, swap.slot[i], fmt.format);
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
	if (!feat.samplerAnisotropy) {
		return 0;
	}
	u32 score = 1;
	if (prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
		score += 0x100;
	} else if (prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
		score += 0x10;
	}
	printf("GPU '%*s' assigned score of 0x%" PRIx32 ".\n",
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
	VkPhysicalDeviceFeatures feat = {
		.samplerAnisotropy = VK_TRUE,
	};
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
	mat4 model;
	mat4 view;
	mat4 proj;
} transforms;

VkDescriptorSetLayout descriptor_set_lyt_create_or_crash(VkDevice logical)
{
	VkDescriptorSetLayoutBinding bind_desc[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		}
	};
	VkDescriptorSetLayoutCreateInfo lyt_desc = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = ARRAY_SIZE(bind_desc),
		.pBindings = bind_desc,
	};
	VkDescriptorSetLayout lyt;
	if (vkCreateDescriptorSetLayout(logical, &lyt_desc, NULL, &lyt) != VK_SUCCESS)
		crash("vkCreateDescriptorSetLayout");
	return lyt;
}

// TODO: this could take a VkDescriptorSetLayoutBinding array instead of being hard coded
VkDescriptorPool descr_pool_create_or_crash(VkDevice logical)
{
	VkDescriptorPoolCreateInfo pool_desc = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = 2,
		.pPoolSizes = (VkDescriptorPoolSize[]){
			{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = (u32) MAX_FRAMES_RENDERING,
			},
			{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = (u32) MAX_FRAMES_RENDERING,
			},
		},
		.maxSets = (u32) MAX_FRAMES_RENDERING,
	};
	VkDescriptorPool pool;
	if (vkCreateDescriptorPool(logical, &pool_desc, NULL, &pool) != VK_SUCCESS)
		crash("vkCreateDescriptorPool");
	return pool;
}

VkDescriptorSet *descr_set_create_or_crash(VkDevice logical,
	VkDescriptorPool pool, VkDescriptorSetLayout lyt)
{
	VkDescriptorSetLayout lyt_dupes[MAX_FRAMES_RENDERING];
	for (u32 i = 0; i < MAX_FRAMES_RENDERING; i++) {
		lyt_dupes[i] = lyt;
	}
	VkDescriptorSet *set = malloc(MAX_FRAMES_RENDERING * sizeof *set);
	VkDescriptorSetAllocateInfo alloc_desc = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = pool,
		.descriptorSetCount = (u32) MAX_FRAMES_RENDERING,
		.pSetLayouts = lyt_dupes,
	};
	if (vkAllocateDescriptorSets(logical, &alloc_desc, set) != VK_SUCCESS)
		crash("vkAllocateDescriptorSets");
	return set;
}

u32 constrain_memory_type_or_crash(VkPhysicalDevice physical, u32 allowed, VkMemoryPropertyFlags cons)
{
	// TODO: we don't really need to access the physical device
	// this late. this could be queried once at device creation
	VkPhysicalDeviceMemoryProperties props;
	vkGetPhysicalDeviceMemoryProperties(physical, &props);

	for (u32 i = 0; i < props.memoryTypeCount; i++) {
		if (allowed & (1u << i)) {
			if ((props.memoryTypes[i].propertyFlags & cons) == cons) {
				return i;
			}
		}
	}
	crash("no suitable memory type available");
}

typedef struct {
	VkBuffer buf;
	VkDeviceMemory mem;
	VkDeviceSize size;
} vulkan_buffer;

vulkan_buffer buffer_create_or_crash(VkDevice logical, VkPhysicalDevice physical,
	VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags cons)
{
	VkBufferCreateInfo buf_desc = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	VkBuffer buf;
	if (vkCreateBuffer(logical, &buf_desc, NULL, &buf) != VK_SUCCESS)
		crash("vkCreateBuffer");
	VkMemoryRequirements reqs;
	vkGetBufferMemoryRequirements(logical, buf, &reqs);
	VkMemoryAllocateInfo mem_desc = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = reqs.size,
		.memoryTypeIndex = constrain_memory_type_or_crash(
			physical,
			reqs.memoryTypeBits,
			cons
		),
	};
	VkDeviceMemory mem;
	if (vkAllocateMemory(logical, &mem_desc, NULL, &mem) != VK_SUCCESS)
		crash("vkAllocateMemory");
	vkBindBufferMemory(logical, buf, mem, 0);
	return (vulkan_buffer){ buf, mem, size };
}

void descr_set_config(VkDevice logical, VkDescriptorSet *set, vulkan_buffer buf,
	VkImageView tex_view, VkSampler tex_sm)
{
	for (u32 i = 0; i < MAX_FRAMES_RENDERING; i++) {
		VkDescriptorBufferInfo buf_desc = {
			.buffer = buf.buf,
			.offset = i * sizeof(transforms),
			.range = sizeof(transforms),
		};
		VkDescriptorImageInfo tex_desc = {
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.imageView = tex_view,
			.sampler = tex_sm,
		};
		VkWriteDescriptorSet write_desc[] = {
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = set[i],
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1,
				.pBufferInfo = &buf_desc,
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = set[i],
				.dstBinding = 1,
				.dstArrayElement = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.pImageInfo = &tex_desc,
			},
		};
		vkUpdateDescriptorSets(logical, ARRAY_SIZE(write_desc), write_desc,
			0, NULL);
	}
}

typedef struct {
	VkPipeline line;
	VkPipelineLayout layout;
	VkDescriptorSetLayout set_layout;
	VkDescriptorPool dpool;
	VkDescriptorSet *set;
} pipeline;

typedef struct {
	vec3 pos;
	vec3 col;
	vec2 uv;
} vertex;

static const vertex vertices[] = {
	{ { -0.5f, -0.5f, 0.3f }, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f} },
	{ { +0.5f, -0.5f, 0.3f }, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} },
	{ { +0.5f, +0.5f, 0.3f }, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f} },
	{ { -0.5f, +0.5f, 0.3f }, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f} },
	{ { -0.5f, -0.5f, 0.0f }, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f} },
	{ { +0.5f, -0.5f, 0.0f }, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} },
	{ { +0.5f, +0.5f, 0.0f }, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f} },
	{ { -0.5f, +0.5f, 0.0f }, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f} },
};

static const u16 indices[] = {
	0, 1, 2,
	2, 3, 0,
	4, 5, 6,
	6, 7, 4,
};

pipeline graphics_pipeline_create_or_crash(const char *vert_path, const char *frag_path,
	VkDevice logical, VkExtent2D dims, VkRenderPass gpass, vulkan_buffer ubuf,
	VkImageView tex_view, VkSampler tex_sm)
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
	VkVertexInputAttributeDescription attributes[] = {
		{
			.binding = 0,
			.location = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(vertex, pos),
		},
		{
			.binding = 0,
			.location = 1,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(vertex, col),
		},
		{
			.binding = 0,
			.location = 2,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = offsetof(vertex, uv),
		},
	};
	VkPipelineVertexInputStateCreateInfo vert_lyt_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &(VkVertexInputBindingDescription){
			.binding = 0,
			.stride = sizeof(vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		},
		.vertexAttributeDescriptionCount = ARRAY_SIZE(attributes),
		.pVertexAttributeDescriptions = attributes,
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
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
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
	VkDescriptorSetLayout set_lyt = descriptor_set_lyt_create_or_crash(logical);
	VkDescriptorPool dpool = descr_pool_create_or_crash(logical);
	VkDescriptorSet *set = descr_set_create_or_crash(logical,
		dpool, set_lyt);
	descr_set_config(logical, set, ubuf, tex_view, tex_sm);
	VkPipelineLayoutCreateInfo unif_lyt_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &set_lyt,
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
	return (pipeline){ gpipe, unif_lyt, set_lyt, dpool, set };
}

VkCommandPool command_pool_create_or_crash(VkDevice logical,
	u32 queue, VkCommandPoolCreateFlags flags)
{
	VkCommandPoolCreateInfo pool_desc = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = flags,
		.queueFamilyIndex = queue,
	};
	VkCommandPool pool;
	if (vkCreateCommandPool(logical, &pool_desc, NULL, &pool) != VK_SUCCESS)
		crash("vkCreateCommandPool");
	return pool;
}

void buffer_populate(VkDevice logical, vulkan_buffer b, const void *data)
{
	void *mapped;
	vkMapMemory(logical, b.mem, 0, b.size, 0, &mapped);
	memcpy(mapped, data, b.size);
	vkUnmapMemory(logical, b.mem);
}

void command_buffer_create_or_crash(VkDevice logical, VkCommandPool pool,
	u32 cnt, VkCommandBuffer *cmd)
{
	VkCommandBufferAllocateInfo buf_desc = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = cnt,
	};
	if (vkAllocateCommandBuffers(logical, &buf_desc, cmd) != VK_SUCCESS)
		crash("vkCreateCommandBuffers");
}

void data_transfer(VkDevice logical, vulkan_buffer dst, vulkan_buffer src,
	u32 iqueue, VkQueue queue)
{
	VkCommandPool pool = command_pool_create_or_crash(logical,
		iqueue, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
	VkCommandBuffer cmd;
	command_buffer_create_or_crash(logical, pool, 1, &cmd);
	VkCommandBufferBeginInfo cmd_begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(cmd, &cmd_begin);
	VkBufferCopy copy_desc = {
		.srcOffset = 0,
		.dstOffset = 0,
		.size = dst.size,
	};
	vkCmdCopyBuffer(cmd, src.buf, dst.buf, 1, &copy_desc);
	vkEndCommandBuffer(cmd);
	VkSubmitInfo submission = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd,
	};
	vkQueueSubmit(queue, 1, &submission, VK_NULL_HANDLE);
	vkQueueWaitIdle(queue);
	vkFreeCommandBuffers(logical, pool, 1, &cmd);
	vkDestroyCommandPool(logical, pool, NULL);
}

vulkan_buffer data_upload(VkDevice logical, VkPhysicalDevice physical,
	VkDeviceSize size, const void *data, u32 iqueue, VkQueue queue, VkBufferUsageFlags usage)
{
	vulkan_buffer staging = buffer_create_or_crash(logical, physical,
		size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	buffer_populate(logical, staging, data);
	vulkan_buffer uploaded = buffer_create_or_crash(logical, physical,
		size, VK_BUFFER_USAGE_TRANSFER_DST_BIT|usage,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	data_transfer(logical, uploaded, staging, iqueue, queue);
	vkDestroyBuffer(logical, staging.buf, NULL);
	vkFreeMemory(logical, staging.mem, NULL);
	return uploaded;
}

typedef struct {
	int width;
	int height;
	void *mem;
} image;

image load_image(const char *path)
{
	int w, h, ch;
	void *ptr = stbi_load(path, &w, &h, &ch, 4);
	if (!ptr) crash("stbi_load");
	return (image){ w, h, ptr };
}

typedef struct {
	VkImage img;
	VkDeviceMemory mem;
	u32 width;
	u32 height;
} vulkan_image;

vulkan_image vulkan_image_create(VkDevice logical, VkPhysicalDevice physical,
	VkFormat fmt, u32 width, u32 height, VkImageUsageFlags usage,
	VkMemoryPropertyFlags prop, VkImageTiling tiling)
{
	VkImageCreateInfo img_desc = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.extent.width = width,
		.extent.height = height,
		.extent.depth = 1u,
		.mipLevels = 1,
		.arrayLayers = 1,
		.format = fmt,
		.tiling = tiling,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.samples = VK_SAMPLE_COUNT_1_BIT,
	};
	VkImage vulkan_img;
	if (vkCreateImage(logical, &img_desc, NULL, &vulkan_img) != VK_SUCCESS)
		crash("vkCreateImage");

	VkMemoryRequirements reqs;
	vkGetImageMemoryRequirements(logical, vulkan_img, &reqs);
	VkMemoryAllocateInfo alloc_desc = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = reqs.size,
		.memoryTypeIndex = constrain_memory_type_or_crash(
			physical,
			reqs.memoryTypeBits,
			prop),
	};
	VkDeviceMemory mem;
	if (vkAllocateMemory(logical, &alloc_desc, NULL, &mem) != VK_SUCCESS)
		crash("vkAllocateMemory");
	vkBindImageMemory(logical, vulkan_img, mem, 0);
	return (vulkan_image){ vulkan_img, mem, width, height };
}

void image_barrier(VkCommandBuffer cmd, VkImage img,
	VkImageLayout prev, VkImageLayout next)
{
	VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.oldLayout = prev,
		.newLayout = next,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = img,
		.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.subresourceRange.baseMipLevel = 0,
		.subresourceRange.levelCount = 1,
		.subresourceRange.baseArrayLayer = 0,
		.subresourceRange.layerCount = 1,
	};
	VkPipelineStageFlags rel_stg, acq_stg;
	if (prev == VK_IMAGE_LAYOUT_UNDEFINED) {
		rel_stg = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		barrier.srcAccessMask = 0;
	} else if (prev == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		rel_stg = VK_PIPELINE_STAGE_TRANSFER_BIT;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	} else {
		crash("unimplemented image transition source");
	}
	if (next == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		acq_stg = VK_PIPELINE_STAGE_TRANSFER_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	} else if (next == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		acq_stg = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	} else {
		crash("unimplemented image transition destination");
	}
	vkCmdPipelineBarrier(cmd,
		rel_stg, acq_stg, 0 /* TODO: VK_DEPENDENCY_BY_REGION_BIT */,
		0, NULL, 0, NULL, 1, &barrier);
}

void image_transfer(VkCommandBuffer cmd, vulkan_buffer buf, vulkan_image img)
{
	VkBufferImageCopy region = {
		.bufferOffset = 0,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.imageSubresource.mipLevel = 0,
		.imageSubresource.baseArrayLayer = 0,
		.imageSubresource.layerCount = 1,
		.imageOffset = {0, 0, 0},
		.imageExtent = {img.width, img.height, 1},
	};
	vkCmdCopyBufferToImage(cmd, buf.buf, img.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

vulkan_image image_upload(VkDevice logical, VkPhysicalDevice physical, image img,
	u32 ixfer_queue, VkQueue xfer_queue)
{
	VkDeviceSize size = (VkDeviceSize) img.width * (VkDeviceSize) img.height * 4ul;
	vulkan_buffer staging = buffer_create_or_crash(logical, physical,
		size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	buffer_populate(logical, staging, img.mem);
	stbi_image_free(img.mem);
	vulkan_image vimg = vulkan_image_create(logical, physical,
		VK_FORMAT_R8G8B8A8_SRGB, (u32) img.width, (u32) img.height,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_TILING_OPTIMAL);
	VkCommandPool pool = command_pool_create_or_crash(logical,
		ixfer_queue, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
	VkCommandBuffer cmd;
	command_buffer_create_or_crash(logical, pool, 1, &cmd);
	VkCommandBufferBeginInfo cmd_begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(cmd, &cmd_begin);
	image_barrier(cmd, vimg.img,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	image_transfer(cmd, staging, vimg);
	image_barrier(cmd, vimg.img,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	VkSubmitInfo submission = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd,
	};
	vkEndCommandBuffer(cmd);
	vkQueueSubmit(xfer_queue, 1, &submission, VK_NULL_HANDLE);
	vkQueueWaitIdle(xfer_queue);
	vkFreeCommandBuffers(logical, pool, 1, &cmd);
	vkDestroyBuffer(logical, staging.buf, NULL);
	vkFreeMemory(logical, staging.mem, NULL);
	vkDestroyCommandPool(logical, pool, NULL);
	return vimg;
}

void command_buffer_record_or_crash(VkCommandBuffer cbuf, VkRenderPass pass,
	swapchain swap, pipeline pipe, VkFramebuffer *framebuf,
	vulkan_buffer vbuf, vulkan_buffer ibuf, u32 upcoming_index)
{
	VkCommandBufferBeginInfo cmd_desc = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
	if (vkBeginCommandBuffer(cbuf, &cmd_desc) != VK_SUCCESS)
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
	vkCmdBeginRenderPass(cbuf, &pass_desc, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(cbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.line);
	vkCmdBindVertexBuffers(cbuf, 0, 1, &vbuf.buf, &(VkDeviceSize){0});
	vkCmdBindIndexBuffer(cbuf, ibuf.buf, 0, VK_INDEX_TYPE_UINT16);
	vkCmdBindDescriptorSets(cbuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipe.layout, 0, 1, &pipe.set[upcoming_index], 0, NULL);
	u32 indx_cnt = (u32) ARRAY_SIZE(indices);
	vkCmdDrawIndexed(cbuf, indx_cnt, 1, 0, 0, 0);
	vkCmdEndRenderPass(cbuf);
	if (vkEndCommandBuffer(cbuf) != VK_SUCCESS)
		crash("vkEndCommandBuffer");
}

typedef struct {
	VkSemaphore present_ready[MAX_FRAMES_RENDERING];
	VkSemaphore render_done[MAX_FRAMES_RENDERING];
	VkFence rendering[MAX_FRAMES_RENDERING];
} fences;

void sync_create_or_crash(VkDevice logical, u32 cnt,
	VkSemaphore *present_ready, VkSemaphore *render_done, VkFence *rendering)
{
	VkSemaphoreCreateInfo sem_desc = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};
	VkFenceCreateInfo fence_desc = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};
	for (u32 i = 0; i < cnt; i++) {
		if (vkCreateSemaphore(logical, &sem_desc, NULL, &present_ready[i]) != VK_SUCCESS)
			crash("vkCreateSemaphore");
		if (vkCreateSemaphore(logical, &sem_desc, NULL, &render_done[i]) != VK_SUCCESS)
			crash("vkCreateSemaphore");
		if (vkCreateFence(logical, &fence_desc, NULL, &rendering[i]) != VK_SUCCESS)
			crash("vkCreateFence");
	}
}

void transforms_upload(void *to, float width, float height)
{
	float time = (float) glfwGetTime();
	float asp = width / height;
	transforms tfm;
	glm_rotate_make(tfm.model, time, (vec3){ 0.0f, 0.0f, 1.0f });
	glm_lookat(
		(vec3){ 2.0f, 2.0f, 2.0f },
		(vec3){ 0.0f, 0.0f, 0.0f },
		(vec3){ 0.0f, 0.0f, 1.0f },
		tfm.view
	);
	glm_perspective((float) M_PI/4.0f, asp, 0.1f, 10.0f, tfm.proj);
	tfm.proj[1][1] *= -1.0f;
	memcpy(to, &tfm, sizeof tfm);
}

typedef struct {
	fences sync;
	VkCommandBuffer commands[MAX_FRAMES_RENDERING];
} draw_calls;

void draw_or_crash(logical_interface interf, draw_calls info, u32 upcoming_index,
	swapchain swap, VkFramebuffer *framebuf, VkRenderPass graphics_pass,
	pipeline pipe, vulkan_buffer vbuf, vulkan_buffer ibuf, transforms *umapped)
{
	// cpu wait for current frame to be done rendering
	vkWaitForFences(interf.logical, 1, &info.sync.rendering[upcoming_index], VK_TRUE, UINT64_MAX);
	vkResetFences(interf.logical, 1, &info.sync.rendering[upcoming_index]);
	vkAcquireNextImageKHR(interf.logical, swap.chain, UINT64_MAX,
		info.sync.present_ready[upcoming_index], VK_NULL_HANDLE, &swap.current);
	// recording commands for next frame
	vkResetCommandBuffer(info.commands[upcoming_index], 0);
	transforms_upload(&umapped[upcoming_index], (float) swap.dim.width, (float) swap.dim.height);
	command_buffer_record_or_crash(info.commands[upcoming_index],
		graphics_pass, swap, pipe, framebuf, vbuf, ibuf, upcoming_index);
	// submitting commands for next frame
	VkSubmitInfo submission_desc = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &info.sync.present_ready[upcoming_index],
		.pWaitDstStageMask = &(VkPipelineStageFlags){
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		},
		.commandBufferCount = 1,
		.pCommandBuffers = &info.commands[upcoming_index],
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &info.sync.render_done[upcoming_index],
	};
	if (vkQueueSubmit(interf.graphics, 1, &submission_desc, info.sync.rendering[upcoming_index]) != VK_SUCCESS)
		crash("vkQueueSubmit");
	// present rendered image
	VkPresentInfoKHR present_desc = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &info.sync.render_done[upcoming_index],
		.swapchainCount = 1,
		.pSwapchains = &swap.chain,
		.pImageIndices = &swap.current,
	};
	vkQueuePresentKHR(interf.present, &present_desc);
}

VkSampler sampler_create_or_crash(VkDevice logical, VkPhysicalDevice physical)
{
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(physical, &props);
	VkSamplerCreateInfo sm_desc = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.anisotropyEnable = VK_TRUE,
		.maxAnisotropy = props.limits.maxSamplerAnisotropy,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
		.unnormalizedCoordinates = VK_FALSE,
		.compareEnable = VK_FALSE,
		.compareOp = VK_COMPARE_OP_ALWAYS,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.mipLodBias = 0.0f,
		.minLod = 0.0f,
		.maxLod = 0.0f,
	};
	VkSampler sm;
	if (vkCreateSampler(logical, &sm_desc, NULL, &sm) != VK_SUCCESS)
		crash("vkCreateSampler");
	return sm;
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
	vulkan_buffer ubuf = buffer_create_or_crash(interf.logical, physical,
		MAX_FRAMES_RENDERING * sizeof(transforms),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	vulkan_image tex_image = image_upload(interf.logical, physical,
		load_image("res/sky_bottom.png"), queues.graphics, interf.graphics);
	VkImageView tex_view = image_view_create(interf.logical, tex_image.img,
		VK_FORMAT_R8G8B8A8_SRGB);
	VkSampler sampler = sampler_create_or_crash(interf.logical, physical);
	pipeline pipe = graphics_pipeline_create_or_crash("bin/shader.vert.spv", "bin/shader.frag.spv",
		interf.logical, swap.dim, graphics_pass, ubuf, tex_view, sampler);
	vulkan_buffer vbuf = data_upload(interf.logical, physical,
		sizeof vertices, vertices, queues.graphics, interf.graphics,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	vulkan_buffer ibuf = data_upload(interf.logical, physical,
		sizeof indices, indices, queues.graphics, interf.graphics,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
	void *umapped;
	vkMapMemory(interf.logical, ubuf.mem, 0, ubuf.size, 0, &umapped);
	VkCommandPool pool = command_pool_create_or_crash(interf.logical,
		queues.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	draw_calls draws;
        command_buffer_create_or_crash(interf.logical, pool, MAX_FRAMES_RENDERING, draws.commands);
	sync_create_or_crash(interf.logical, MAX_FRAMES_RENDERING,
		draws.sync.present_ready, draws.sync.render_done, draws.sync.rendering);

	u32 cpu_frame = 0;
	while (!glfwWindowShouldClose(win)) {
		double beg_time = glfwGetTime();
		glfwPollEvents();
		draw_or_crash(interf, draws, cpu_frame % MAX_FRAMES_RENDERING,
			swap, framebuf, graphics_pass, pipe, vbuf, ibuf, umapped);
		cpu_frame++;
		double end_time = glfwGetTime();
		printf("\rframe time: %fms", (end_time - beg_time) * 1e3);
	}
	printf("\n");

	vkDeviceWaitIdle(interf.logical);
	for (u32 i = 0; i < MAX_FRAMES_RENDERING; i++) {
		vkDestroySemaphore(interf.logical, draws.sync.present_ready[i], NULL);
		vkDestroySemaphore(interf.logical, draws.sync.render_done[i], NULL);
		vkDestroyFence(interf.logical, draws.sync.rendering[i], NULL);
	}
	vkFreeCommandBuffers(interf.logical, pool, MAX_FRAMES_RENDERING, draws.commands);
	vkDestroyCommandPool(interf.logical, pool, NULL);
	vkDestroyBuffer(interf.logical, ubuf.buf, NULL);
	vkFreeMemory(interf.logical, ubuf.mem, NULL);
	vkFreeMemory(interf.logical, ibuf.mem, NULL);
	vkDestroyBuffer(interf.logical, ibuf.buf, NULL);
	vkFreeMemory(interf.logical, vbuf.mem, NULL);
	vkDestroyBuffer(interf.logical, vbuf.buf, NULL);
	vkDestroyPipeline(interf.logical, pipe.line, NULL);
	vkDestroyPipelineLayout(interf.logical, pipe.layout, NULL);
	vkDestroySampler(interf.logical, sampler, NULL);
	vkDestroyImageView(interf.logical, tex_view, NULL);
	vkDestroyImage(interf.logical, tex_image.img, NULL);
	vkFreeMemory(interf.logical, tex_image.mem, NULL);
	vkDestroyDescriptorPool(interf.logical, pipe.dpool, NULL);
	vkDestroyDescriptorSetLayout(interf.logical, pipe.set_layout, NULL);
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

