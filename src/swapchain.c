#include <stdlib.h>
#include "swapchain.h"
#include "util.h"
#include "image.h"

static VkExtent2D swapchain_select_resolution(context *ctx,
	VkSurfaceCapabilitiesKHR *pcap)
{
	VkSurfaceCapabilitiesKHR cap;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physical_device,
		ctx->window_surface, &cap);
	*pcap = cap;
	if (cap.currentExtent.width == UINT32_MAX) {
		int width, height;
		glfwGetFramebufferSize(ctx->window, &width, &height);
		return (VkExtent2D){
			CLAMP((u32) width, cap.minImageExtent.width, cap.maxImageExtent.height),
			CLAMP((u32) height, cap.minImageExtent.height, cap.maxImageExtent.height),
		};
	} else {
		return cap.currentExtent;
	}
}

vulkan_swapchain vulkan_swapchain_create(context *ctx)
{
	VkSurfaceKHR surface = ctx->window_surface;
	VkSurfaceCapabilitiesKHR cap;
	VkExtent2D dim = swapchain_select_resolution(ctx, &cap);
	VkPresentModeKHR mode = ctx->present_mode;
	VkSurfaceFormatKHR fmt = ctx->present_surface_fmt;
	u32 expected_swap_cnt = cap.minImageCount + 1u;
	VkSwapchainCreateInfoKHR swap_desc = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = (cap.maxImageCount != 0)?
			MIN(cap.maxImageCount, expected_swap_cnt):
			expected_swap_cnt,
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
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	vulkan_swapchain swap;
	if (vkCreateSwapchainKHR(ctx->device,
		&swap_desc, NULL, &swap.handle) != VK_SUCCESS)
		crash("vkCreateSwapchainKHR");
	vkGetSwapchainImagesKHR(ctx->device, swap.handle, &swap.n_slot, NULL);
	swap.slot = xmalloc(swap.n_slot * (sizeof(*swap.slot) + sizeof(*swap.view)));
	vkGetSwapchainImagesKHR(ctx->device, swap.handle, &swap.n_slot, swap.slot);
	swap.fmt = fmt.format;
	swap.dim = dim;
	swap.view = (void*) ((char*) swap.slot + swap.n_slot * sizeof(*swap.slot));
	for (u32 i = 0; i < swap.n_slot; i++) {
		swap.view[i] = vulkan_image_view_create_external(ctx, swap.slot[i],
			fmt.format, 1u, 1u, VK_IMAGE_ASPECT_COLOR_BIT);
	}
	return swap;
}

void vulkan_swapchain_destroy(context *ctx, vulkan_swapchain *sc)
{
	for (u32 i = 0; i < sc->n_slot; i++) {
		vulkan_image_view_destroy(ctx, sc->view[i]);
	}
	free(sc->slot);
	vkDestroySwapchainKHR(ctx->device, sc->handle, NULL);
}

