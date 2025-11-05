#ifndef GALA_SWAPCHAIN_H
#define GALA_SWAPCHAIN_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "types.h"
#include "gpu.h"


#define MAX_FRAMES_RENDERING (2)

typedef struct {
	VkSwapchainKHR handle;
	VkImage *slot;
	VkImageView *view;
	u32 n_slot;
	u32 i_slot;
	VkFormat fmt;
	VkExtent2D dim;
} vulkan_swapchain;

vulkan_swapchain vulkan_swapchain_create(context *ctx);
void vulkan_swapchain_destroy(context *ctx, vulkan_swapchain *sc);

#endif /* GALA_SWAPCHAIN_H */

