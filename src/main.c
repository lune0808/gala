#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>
#include <assert.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>


typedef uint32_t u32;

void *xmalloc(size_t sz)
{
	void *ptr = malloc(sz);
	if (!ptr) {
		fprintf(stderr, "malloc\n");
		exit(1);
	}
	return ptr;
}

void init_glfw_or_crash()
{
	if (glfwInit() != GLFW_TRUE) {
		fprintf(stderr, "glfwInit\n");
		exit(1);
	}
}

GLFWwindow *init_window_or_crash(int width, int height, const char *title)
{
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow *win = glfwCreateWindow(width, height, title, NULL, NULL);
	if (!win) {
		fprintf(stderr, "glfwCreateWindow\n");
		exit(1);
	}
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
	fprintf(stderr, "validation layer '%s' not found\n", validation);
	exit(1);
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
	if (status != VK_SUCCESS) {
		fprintf(stderr, "vkCreateInstance\n");
		exit(1);
	}
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

typedef struct {
	VkSwapchainKHR chain;
	VkImage *slot;
	VkImageView *view;
	u32 n_slot;
	VkFormat fmt;
	VkExtent2D dim;
} swapchain;

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
	if (vkCreateImageView(logical, &view_desc, NULL, &view) != VK_SUCCESS) {
		fprintf(stderr, "vkCreateInstance\n");
		exit(1);
	}
	return view;
}

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
	if (vkCreateSwapchainKHR(logical, &swap_desc, NULL, &swap.chain) != VK_SUCCESS) {
		fprintf(stderr, "vkCreateSwapchainKHR\n");
		exit(1);
	}
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
	if (selected == VK_NULL_HANDLE) {
		fprintf(stderr, "suitable processor not found.\n");
		exit(1);
	}
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
	if (vkCreateDevice(physical, &logdev_desc, NULL, &i.logical) != VK_SUCCESS) {
		fprintf(stderr, "couldn't create vulkan logical device");
		exit(1);
	}
	vkGetDeviceQueue(i.logical, queues->graphics, 0, &i.graphics);
	vkGetDeviceQueue(i.logical, queues->present, !sep_queues, &i.present);
	return i;
}

VkSurfaceKHR window_surface(VkInstance inst, GLFWwindow *win)
{
	VkSurfaceKHR surface;
	if (glfwCreateWindowSurface(inst, win, NULL, &surface) != VK_SUCCESS) {
		fprintf(stderr, "couldn't create a surface to present to\n");
		exit(1);
	}
	return surface;
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
	while (!glfwWindowShouldClose(win)) {
		glfwPollEvents();
	}
	for (u32 i = 0; i < swap.n_slot; i++) {
		vkDestroyImageView(interf.logical, swap.view[i], NULL);
	}
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

