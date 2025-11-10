#ifndef GALA_SWAPCHAIN_H
#define GALA_SWAPCHAIN_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "types.h"
#include "gpu.h"
#include "hwqueue.h"
#include "image.h"
#include "shared.h"


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

typedef struct {
	vulkan_swapchain base;
	vulkan_bound_image depth_buffer;
	VkRenderPass pass;
	VkFramebuffer *framebuffer;
	hw_queue graphics_queue;
	VkCommandPool graphics_pool;
	VkCommandBuffer graphics_cmd[MAX_FRAMES_RENDERING];
	VkSemaphore present_ready[MAX_FRAMES_RENDERING];
	VkSemaphore render_done[MAX_FRAMES_RENDERING];
	VkFence rendering[MAX_FRAMES_RENDERING];
	u32 frame_indx;
} attached_swapchain;

attached_swapchain attached_swapchain_create(context *ctx);
void attached_swapchain_destroy(context *ctx, attached_swapchain *sc);
VkCommandBuffer attached_swapchain_current_graphics_cmd(attached_swapchain *sc);
VkSemaphore *attached_swapchain_current_present_ready(attached_swapchain *sc);
VkSemaphore *attached_swapchain_current_render_done(attached_swapchain *sc);
VkFence attached_swapchain_current_rendering(attached_swapchain *sc);
void attached_swapchain_swap_buffers(context *ctx, attached_swapchain *sc);
void attached_swapchain_present(attached_swapchain *sc);

#endif /* GALA_SWAPCHAIN_H */

