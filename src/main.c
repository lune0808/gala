#include <stdio.h>
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
	u32 graphics;
	u32 missing;
} queue_families;

queue_families required_queue_families(VkPhysicalDevice dev)
{
	queue_families req;
	req.missing = 1u;
	u32 n_qf;
	vkGetPhysicalDeviceQueueFamilyProperties(dev, &n_qf, NULL);
	VkQueueFamilyProperties *qf = xmalloc(n_qf * sizeof *qf);
	vkGetPhysicalDeviceQueueFamilyProperties(dev, &n_qf, qf);
	for (u32 i = 0; i < n_qf; i++) {
		if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			req.graphics = i;
			req.missing &= ~1u;
			break;
		}
	}
	free(qf);
	return req;
}

u32 processor_score(VkPhysicalDevice dev, queue_families *f)
{
	*f = required_queue_families(dev);
	if (f->missing) {
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
	printf("%*s assigned score of %" PRIu32 ".\n",
		(int) VK_MAX_PHYSICAL_DEVICE_NAME_SIZE, prop.deviceName, score);
	return score;
}

VkPhysicalDevice vulkan_select_gpu_or_crash(VkInstance inst, queue_families *queues)
{
	VkPhysicalDevice selected = VK_NULL_HANDLE;
	u32 n_gpu;
	vkEnumeratePhysicalDevices(inst, &n_gpu, NULL);
	VkPhysicalDevice *gpu = xmalloc(n_gpu * sizeof *gpu);
	vkEnumeratePhysicalDevices(inst, &n_gpu, gpu);
	u32 best_score = 0;
	for (u32 i = 0; i < n_gpu; i++) {
		queue_families cur_queues;
		u32 score = processor_score(gpu[i], &cur_queues);
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

VkDevice vulkan_logical_device_or_crash(VkPhysicalDevice physical, queue_families *queues)
{
	float prio = 1.0f;
	VkDeviceQueueCreateInfo queue_desc = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = queues->graphics,
		.queueCount = 1,
		.pQueuePriorities = &prio,
	};
	VkPhysicalDeviceFeatures feat = {};
	VkDeviceCreateInfo logdev_desc = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pQueueCreateInfos = &queue_desc,
		.queueCreateInfoCount = 1,
		.pEnabledFeatures = &feat,
		.enabledExtensionCount = 0,
		.enabledLayerCount = 1,
		.ppEnabledLayerNames = &validation,
	};
	VkDevice logical;
	if (vkCreateDevice(physical, &logdev_desc, NULL, &logical) != VK_SUCCESS) {
		fprintf(stderr, "couldn't create vulkan logical device");
		exit(1);
	}
	return logical;
}

VkQueue vulkan_graphics_queue(VkDevice logical, queue_families *queues)
{
	VkQueue q;
	vkGetDeviceQueue(logical, queues->graphics, 0, &q);
	return q;
}

static const int WIDTH = 800;
static const int HEIGHT = 600;

int main()
{
	init_glfw_or_crash();
	GLFWwindow *win = init_window_or_crash(WIDTH, HEIGHT, "Gala");
	u32 n_ext;
	vkEnumerateInstanceExtensionProperties(NULL, &n_ext, NULL);
	printf("%" PRIu32 " extensions supported for Vulkan\n", n_ext);
	VkInstance inst = vulkan_instance_or_crash();
	queue_families queues;
	VkPhysicalDevice physical = vulkan_select_gpu_or_crash(inst, &queues);
	VkDevice logical = vulkan_logical_device_or_crash(physical, &queues);
	VkQueue gfx_q = vulkan_graphics_queue(logical, &queues);
	while (!glfwWindowShouldClose(win)) {
		glfwPollEvents();
	}
	vkDestroyDevice(logical, NULL);
	vkDestroyInstance(inst, NULL);
	glfwDestroyWindow(win);
	glfwTerminate();
	return 0;
}

