#include <stdlib.h>
#include <assert.h>
#include "swapchain.h"
#include "util.h"
#include "sync.h"
#include "image.h"

vulkan_swapchain vulkan_swapchain_create(context *ctx)
{
	VkSurfaceKHR surface = ctx->present_surface.handle;
	VkSurfaceCapabilitiesKHR *cap = &ctx->present_surface.limits;
	VkExtent2D dim = ctx->present_surface.dim;
	VkPresentModeKHR mode = ctx->present_surface.mode;
	VkSurfaceFormatKHR fmt = ctx->present_surface.fmt;
	u32 expected_swap_cnt = cap->minImageCount + 1u;
	VkSwapchainCreateInfoKHR swap_desc = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = (cap->maxImageCount != 0)?
			MIN(cap->maxImageCount, expected_swap_cnt):
			expected_swap_cnt,
		.imageFormat = fmt.format,
		.imageColorSpace = fmt.colorSpace,
		.imageExtent = dim,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = cap->currentTransform,
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

static VkFramebuffer *framebuf_attach(VkDevice logical, vulkan_swapchain *swap, VkRenderPass pass, VkImageView depth_view)
{
	VkFramebuffer *framebuf = xmalloc(swap->n_slot * sizeof *framebuf);
	for (u32 i = 0; i < swap->n_slot; i++) {
		VkImageView attacht[] = {
			swap->view[i],
			depth_view,
		};
		framebuf[i] = framebuffer_attach(logical, pass,
			ARRAY_SIZE(attacht), attacht, swap->dim);
	}
	return framebuf;
}

static vulkan_bound_image depth_buffer_create(context *ctx, VkExtent2D dims)
{
	VkFormat candidates[] = {
		VK_FORMAT_D32_SFLOAT,
		VK_FORMAT_D24_UNORM_S8_UINT,
		VK_FORMAT_D32_SFLOAT_S8_UINT,
	};
	VkFormat fmt = constrain_format(ctx->physical_device,
		ARRAY_SIZE(candidates), candidates,
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
	if (fmt == VK_FORMAT_UNDEFINED)
		crash("no suitable format found for a depth buffer");
	VkImageCreateInfo desc = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = fmt,
		.extent = { dims.width, dims.height, 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	return vulkan_bound_image_create(ctx, &desc,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void attachment_desc(VkFormat fmt, u32 index,
	VkAttachmentDescription *attach, VkAttachmentReference *ref,
	VkAttachmentLoadOp on_load, VkAttachmentStoreOp on_store,
	VkImageLayout render, VkImageLayout release)
{
	attach[index].flags = 0;
	attach[index].format = fmt;
	attach[index].samples = VK_SAMPLE_COUNT_1_BIT;
	attach[index].loadOp = on_load;
	attach[index].storeOp = on_store;
	attach[index].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attach[index].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attach[index].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attach[index].finalLayout = release;
	ref[index].attachment = index;
	ref[index].layout = render;
}

static VkRenderPass render_pass_create(VkDevice logical, VkFormat fmt,
	VkFormat depth_fmt)
{
	VkAttachmentDescription attach[2];
	VkAttachmentReference refs[2];
	attachment_desc(fmt, 0, attach, refs,
		VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	attachment_desc(depth_fmt, 1, attach, refs,
		VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	VkSubpassDescription subpass_desc = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &refs[0],
		.pDepthStencilAttachment = &refs[1],
	};
	VkSubpassDependency draw_dep[] = {
		{
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.srcStageMask =
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
				| VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.dstSubpass = 0,
			.dstStageMask =
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
				| VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			.dstAccessMask =
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
				| VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		},
		{
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
			.dstSubpass = 0,
			.dstStageMask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
			.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
		},
	};
	VkRenderPassCreateInfo pass_desc = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = ARRAY_SIZE(attach),
		.pAttachments = attach,
		.subpassCount = 1,
		.pSubpasses = &subpass_desc,
		.dependencyCount = ARRAY_SIZE(draw_dep),
		.pDependencies = draw_dep,
	};
	VkRenderPass pass;
	if (vkCreateRenderPass(logical, &pass_desc, NULL, &pass) != VK_SUCCESS)
		crash("vkCreateRenderPass");
	return pass;
}

attached_swapchain attached_swapchain_create(context *ctx)
{
	attached_swapchain sc;
	sc.base = vulkan_swapchain_create(ctx);
	sc.depth_buffer = depth_buffer_create(ctx, sc.base.dim);
	sc.pass = render_pass_create(ctx->device,
		sc.base.fmt, sc.depth_buffer.fmt);
	sc.framebuffer = framebuf_attach(ctx->device,
		&sc.base, sc.pass, sc.depth_buffer.view);
	sc.graphics_queue = hw_queue_ref(ctx, ctx->specs->iq_graphics);
	sc.graphics_pool = command_pool_create(ctx->device, sc.graphics_queue,
		VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	command_buffer_create(ctx->device, sc.graphics_pool,
		MAX_FRAMES_RENDERING, sc.graphics_cmd);
	gpu_fence_create(ctx->device,
		MAX_FRAMES_RENDERING, sc.present_ready);
	gpu_fence_create(ctx->device,
		MAX_FRAMES_RENDERING, sc.render_done);
	cpu_fence_create(ctx->device,
		MAX_FRAMES_RENDERING, sc.rendering, VK_FENCE_CREATE_SIGNALED_BIT);
	sc.frame_indx = 0;
	return sc;
}

VkCommandBuffer attached_swapchain_current_graphics_cmd(attached_swapchain *sc)
{
	return sc->graphics_cmd[sc->frame_indx];
}

VkSemaphore *attached_swapchain_current_present_ready(attached_swapchain *sc)
{
	return &sc->present_ready[sc->frame_indx];
}

VkSemaphore *attached_swapchain_current_render_done(attached_swapchain *sc)
{
	return &sc->render_done[sc->frame_indx];
}

VkFence attached_swapchain_current_rendering(attached_swapchain *sc)
{
	return sc->rendering[sc->frame_indx];
}

void attached_swapchain_destroy(context *ctx, attached_swapchain *sc)
{
	vkDestroyCommandPool(ctx->device, sc->graphics_pool, NULL);
	for (u32 i = 0; i < MAX_FRAMES_RENDERING; i++) {
		vkDestroyFence(ctx->device, sc->rendering[i], NULL);
		vkDestroySemaphore(ctx->device, sc->render_done[i], NULL);
		vkDestroySemaphore(ctx->device, sc->present_ready[i], NULL);
	}
	for (u32 i = 0; i < sc->base.n_slot; i++) {
		vkDestroyFramebuffer(ctx->device, sc->framebuffer[i], NULL);
	}
	free(sc->framebuffer);
	vkDestroyRenderPass(ctx->device, sc->pass, NULL);
	vulkan_bound_image_destroy(ctx, &sc->depth_buffer);
	vulkan_swapchain_destroy(ctx, &sc->base);
}

void attached_swapchain_swap_buffers(context *ctx, attached_swapchain *sc)
{
	sc->frame_indx = (sc->frame_indx + 1) % MAX_FRAMES_RENDERING;
	cpu_fence_wait_one(ctx->device,
		attached_swapchain_current_rendering(sc), UINT64_MAX);
	vkAcquireNextImageKHR(ctx->device, sc->base.handle, UINT64_MAX,
		*attached_swapchain_current_present_ready(sc),
		VK_NULL_HANDLE, &sc->base.i_slot);
}

void attached_swapchain_present(attached_swapchain *sc)
{
	VkPresentInfoKHR present_desc = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = attached_swapchain_current_render_done(sc),
		.swapchainCount = 1,
		.pSwapchains = &sc->base.handle,
		.pImageIndices = &sc->base.i_slot,
	};
	vkQueuePresentKHR(sc->graphics_queue.handle, &present_desc);
}

