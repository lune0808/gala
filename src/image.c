#include "image.h"
#include "util.h"


vulkan_image vulkan_image_create(context *ctx, VkImageCreateInfo *desc,
	VkMemoryPropertyFlags memory)
{
	VkImage handle;
	if (vkCreateImage(ctx->device, desc, NULL, &handle) != VK_SUCCESS)
		crash("vkCreateImage");
	VkMemoryRequirements req;
	vkGetImageMemoryRequirements(ctx->device, handle, &req);
	extern u32 constrain_memory_type_or_crash(context*, u32, VkMemoryPropertyFlags);
	VkMemoryAllocateInfo alloc_desc = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = req.size,
		.memoryTypeIndex = constrain_memory_type_or_crash(
			ctx, req.memoryTypeBits, memory
		),
	};
	VkDeviceMemory mem;
	if (vkAllocateMemory(ctx->device, &alloc_desc, NULL, &mem) != VK_SUCCESS)
		crash("vkAllocateMemory");
	vkBindImageMemory(ctx->device, handle, mem, 0);
	return (vulkan_image){ handle, mem,
		{ desc->extent.width, desc->extent.height },
		desc->format, desc->mipLevels
	};
}

void vulkan_image_destroy(context *ctx, vulkan_image *img)
{
	vkDestroyImage(ctx->device, img->handle, NULL);
	vkFreeMemory(ctx->device, img->mem, NULL);
}

VkImageView vulkan_image_view_create(context *ctx, vulkan_image *img, u32 n_img,
	VkImageAspectFlags kind)
{
	return vulkan_image_view_create_external(ctx,
		img->handle, img->fmt, img->mips, n_img, kind);
}

VkImageView vulkan_image_view_create_external(context *ctx, VkImage handle,
	VkFormat fmt, u32 mips, u32 n_img, VkImageAspectFlags kind)
{
	VkImageViewCreateInfo desc = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = handle,
		.viewType = (n_img == 1) ?
			VK_IMAGE_VIEW_TYPE_2D:
			VK_IMAGE_VIEW_TYPE_2D_ARRAY,
		.format = fmt,
		.subresourceRange.aspectMask = kind,
		.subresourceRange.baseMipLevel = 0,
		.subresourceRange.levelCount = mips,
		.subresourceRange.baseArrayLayer = 0,
		.subresourceRange.layerCount = n_img,
	};
	VkImageView view;
	if (vkCreateImageView(ctx->device, &desc, NULL, &view) != VK_SUCCESS)
		crash("vkCreateImageView");
	return view;
}

void vulkan_image_view_destroy(context *ctx, VkImageView view)
{
	vkDestroyImageView(ctx->device, view, NULL);
}

vulkan_bound_image_view vulkan_bound_image_view_create(context *ctx,
	VkImageCreateInfo *desc, VkMemoryPropertyFlags memory, VkImageAspectFlags kind)
{
	vulkan_image img = vulkan_image_create(ctx, desc, memory);
	VkImageView view = vulkan_image_view_create(ctx, &img, 1u, kind);
	return (vulkan_bound_image_view){ img, view };
}

void vulkan_bound_image_view_destroy(context *ctx, vulkan_bound_image_view *bnd)
{
	vulkan_image_view_destroy(ctx, bnd->view);
	vulkan_image_destroy(ctx, &bnd->img);
}

