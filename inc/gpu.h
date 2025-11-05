#ifndef GALA_GPU_H
#define GALA_GPU_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "types.h"


typedef struct {
	VkPhysicalDeviceProperties properties;
	VkPhysicalDeviceFeatures features;
	VkPhysicalDeviceMemoryProperties memory;
	VkQueueFamilyProperties *queue_families;
	u32 n_queue_families;
	u32 iq_graphics;
	u32 iq_compute;
	u32 iq_transfer;
} *gpu_specs;

typedef struct {
	GLFWwindow *window;
	VkInstance vk_instance;
	VkSurfaceKHR window_surface;
	VkPhysicalDevice physical_device;
	VkDevice device;
	gpu_specs specs;
} context;

context context_init(int width, int height, const char *title);
void context_fini(context *ctx);
#endif /* GALA_GPU_H */

