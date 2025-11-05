#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "util.h"
#include "gpu.h"


static bool less_bits(u32 a, u32 b)
{
	return a < b;
}

static gpu_specs gpu_specs_init(VkPhysicalDevice dev)
{
	u32 n_queue_families;
	vkGetPhysicalDeviceQueueFamilyProperties(dev, &n_queue_families, NULL);
	gpu_specs specs = malloc(sizeof(*specs)
			+ n_queue_families * sizeof(*specs->queue_families));
	vkGetPhysicalDeviceQueueFamilyProperties(dev,
		&n_queue_families, specs->queue_families);

	// search most fitting queues
	u32 iq_graphics = UINT32_MAX;
	u32 iq_compute  = UINT32_MAX;
	u32 iq_transfer = UINT32_MAX;
	// for the graphics/present queue we want to have everything
	// for other queues we want the most specialized queues
	VkQueueFlags best_f_graphics = 0;
	VkQueueFlags best_f_compute  = VK_QUEUE_FLAG_BITS_MAX_ENUM;
	VkQueueFlags best_f_transfer = VK_QUEUE_FLAG_BITS_MAX_ENUM;
	for (u32 iq = 0; iq < n_queue_families; iq++) {
		VkQueueFlags flags = specs->queue_families[iq].queueFlags;
		VkQueueFlags f_graphics = flags;
		VkQueueFlags f_compute  = flags & best_f_compute;
		VkQueueFlags f_transfer = flags & best_f_transfer;
		if (flags & VK_QUEUE_GRAPHICS_BIT
			&& less_bits(best_f_graphics, f_graphics)) {
			best_f_graphics = f_graphics;
			iq_graphics = iq;
		}
		if (flags & VK_QUEUE_COMPUTE_BIT
			&& less_bits(f_compute, best_f_compute)) {
			best_f_compute = f_compute;
			iq_compute = iq;
		}
		if (flags & VK_QUEUE_TRANSFER_BIT
			&& less_bits(f_transfer, best_f_transfer)) {
			best_f_transfer = f_transfer;
			iq_transfer = iq;
		}
	}
	specs->iq_graphics = iq_graphics;
	specs->iq_compute  = iq_compute;
	specs->iq_transfer = iq_transfer;
	specs->n_queue_families = n_queue_families;

	vkGetPhysicalDeviceProperties(dev, &specs->properties);
	vkGetPhysicalDeviceFeatures(dev, &specs->features);
	vkGetPhysicalDeviceMemoryProperties(dev, &specs->memory);
	return specs;
}

static void gpu_specs_fini(gpu_specs specs)
{
	free(specs);
}

static u32 gpu_specs_score(gpu_specs specs)
{
	if (!specs->features.samplerAnisotropy)
		return 0;
	if (specs->iq_graphics == UINT32_MAX)
		return 0;
	if (specs->iq_compute == UINT32_MAX)
		return 0;
	if (specs->iq_transfer == UINT32_MAX)
		return 0;
	u32 score = 0;
	if (specs->properties.deviceType
		== VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
		score = 0b11;
	} else if (specs->properties.deviceType
		== VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
		score = 0b1;
	}
	return score;
}

static int extension_sort(const void *_e1, const void *_e2)
{
	const VkExtensionProperties *e1 = _e1;
	const VkExtensionProperties *e2 = _e2;
	return strcmp(e1->extensionName, e2->extensionName);
}

static u32 extension_match(VkPhysicalDevice dev,
	u32 n_ext, const char **ext)
{
	u32 n_dev_ext;
	vkEnumerateDeviceExtensionProperties(dev, NULL, &n_dev_ext, NULL);
	if (n_ext > n_dev_ext)
		return 0;
	VkExtensionProperties *dev_ext = xmalloc(n_dev_ext * sizeof(*dev_ext));
	vkEnumerateDeviceExtensionProperties(dev, NULL, &n_dev_ext, dev_ext);
	qsort(dev_ext, n_dev_ext, sizeof(*dev_ext), extension_sort);
	qsort(ext, n_ext, sizeof(*ext), (void*) strcmp);
	u32 i_ext = 0, i_dev_ext = 0;
	while (i_ext < n_ext && i_dev_ext < n_dev_ext) {
		int cmp = strcmp(ext[i_ext], dev_ext[i_dev_ext].extensionName);
		if (cmp >= 0) {
			i_dev_ext++;
		}
		if (cmp <= 0) {
			i_ext++;
		}
	}
	free(dev_ext);
	u32 diff = (i_ext != n_ext);
	return ~diff;
}

static void init_glfw_or_crash()
{
	if (glfwInit() != GLFW_TRUE)
		crash("glfwInit");
}

static GLFWwindow *glfw_window_or_crash(int width, int height, const char *title)
{
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow *window = glfwCreateWindow(width, height, title, NULL, NULL);
	if (!window)
		crash("glfwCreateWindow");
	return window;
}

static const char *const validation = "VK_LAYER_KHRONOS_validation";

static void vulkan_validation_layers_or_crash()
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

static VkInstance vulkan_instance_or_crash()
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

static VkSurfaceKHR vulkan_surface(VkInstance inst, GLFWwindow *win)
{
	VkSurfaceKHR surface;
	if (glfwCreateWindowSurface(inst, win, NULL, &surface) != VK_SUCCESS)
		crash("couldn't create a surface to present to");
	return surface;
}
static const char *extensions[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

static VkPhysicalDevice vulkan_select_gpu_or_crash(VkInstance inst,
	VkSurfaceKHR target, gpu_specs *out_specs)
{
	VkPhysicalDevice selected = VK_NULL_HANDLE;
	u32 n_gpu;
	vkEnumeratePhysicalDevices(inst, &n_gpu, NULL);
	VkPhysicalDevice *gpu = xmalloc(n_gpu * sizeof *gpu);
	vkEnumeratePhysicalDevices(inst, &n_gpu, gpu);
	u32 best_score = 0;
	gpu_specs selected_specs = NULL;
	for (u32 i = 0; i < n_gpu; i++) {
		gpu_specs specs = gpu_specs_init(gpu[i]);
		u32 specs_score = gpu_specs_score(specs);
		specs_score &= extension_match(gpu[i],
			ARRAY_SIZE(extensions), extensions);
		VkBool32 can_present;
		vkGetPhysicalDeviceSurfaceSupportKHR(gpu[i], specs->iq_graphics,
			target, &can_present);	
		specs_score &= can_present;
		if (specs_score > best_score) {
			best_score = specs_score;
			selected = gpu[i];
			gpu_specs_fini(selected_specs);
			selected_specs = specs;
		} else {
			gpu_specs_fini(specs);
		}
	}
	free(gpu);
	if (selected_specs->iq_graphics == UINT32_MAX)
		crash("no suitable graphics queue");
	if (selected_specs->iq_compute == UINT32_MAX)
		crash("no suitable compute queue");
	if (selected_specs->iq_transfer == UINT32_MAX)
		crash("no suitable transfer queue");
	if (selected == VK_NULL_HANDLE)
		crash("suitable processor not found.");
	*out_specs = selected_specs;
	return selected;
}

static VkDevice vulkan_logical_device_or_crash(VkPhysicalDevice physical,
	gpu_specs specs)
{
	VkDeviceQueueCreateInfo queue_desc[] = {
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = specs->iq_graphics,
			.queueCount = 1,
			.pQueuePriorities = (float[]){ 1.0f },
		},
	};
	VkPhysicalDeviceFeatures features = {
		.samplerAnisotropy = VK_TRUE,
	};
	VkDeviceCreateInfo device_desc = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = ARRAY_SIZE(queue_desc),
		.pQueueCreateInfos = queue_desc,
		.pEnabledFeatures = &features,
		.enabledExtensionCount = ARRAY_SIZE(extensions),
		.ppEnabledExtensionNames = extensions,
		.enabledLayerCount = 1,
		.ppEnabledLayerNames = &validation,
	};
	VkDevice device;
	if (vkCreateDevice(physical, &device_desc, NULL, &device) != VK_SUCCESS)
		crash("vkCreateDevice");
	return device;
}

context context_init(int width, int height, const char *title)
{
	init_glfw_or_crash();
	GLFWwindow *window = glfw_window_or_crash(width, height, title);
	VkInstance vk_instance = vulkan_instance_or_crash();
	VkSurfaceKHR window_surface = vulkan_surface(vk_instance, window);
	gpu_specs specs;
	VkPhysicalDevice physical_device = vulkan_select_gpu_or_crash(
		vk_instance, window_surface, &specs);
	VkDevice device = vulkan_logical_device_or_crash(physical_device, specs);
	return (context){ window, vk_instance, window_surface,
		physical_device, device, specs };
}

void context_fini(context *ctx)
{
	gpu_specs_fini(ctx->specs);
	vkDestroyDevice(ctx->device, NULL);
	vkDestroySurfaceKHR(ctx->vk_instance, ctx->window_surface, NULL);
	vkDestroyInstance(ctx->vk_instance, NULL);
	glfwDestroyWindow(ctx->window);
	glfwTerminate();
}

