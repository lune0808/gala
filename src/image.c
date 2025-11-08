#include <math.h>
#include <stb/stb_image.h>
#include "image.h"
#include "util.h"


loaded_image load_image(const char *path)
{
	int w, h, ch;
	void *ptr = stbi_load(path, &w, &h, &ch, 4);
	if (!ptr) crash("stbi_load(\"%s\")", path);
	return (loaded_image){ (u32) w, (u32) h, ptr };
}

void loaded_image_fini(loaded_image img)
{
	stbi_image_free(img.mem);
}

u32 mips_for(u32 width, u32 height)
{
	float max_dim = MAX((float) width, (float) height);
	u32 mips = 1u + (u32) log2f(max_dim);
	return mips;
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

vulkan_bound_image vulkan_bound_image_create(context *ctx,
	VkImageCreateInfo *desc, VkMemoryPropertyFlags memory, VkImageAspectFlags kind)
{
	VkImage handle;
	if (vkCreateImage(ctx->device, desc, NULL, &handle) != VK_SUCCESS)
		crash("vkCreateImage");
	VkMemoryRequirements req;
	vkGetImageMemoryRequirements(ctx->device, handle, &req);
	extern u32 constrain_memory_type(context*, u32, VkMemoryPropertyFlags);
	VkMemoryAllocateInfo alloc_desc = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = req.size,
		.memoryTypeIndex = constrain_memory_type(
			ctx, req.memoryTypeBits, memory
		),
	};
	VkDeviceMemory mem;
	if (vkAllocateMemory(ctx->device, &alloc_desc, NULL, &mem) != VK_SUCCESS)
		crash("vkAllocateMemory");
	vkBindImageMemory(ctx->device, handle, mem, 0);
	VkImageView view = vulkan_image_view_create_external(ctx, handle,
		desc->format, desc->mipLevels, desc->arrayLayers, kind);
	return (vulkan_bound_image){
		handle, view, mem,
		{ desc->extent.width, desc->extent.height },
		desc->format, desc->mipLevels, desc->arrayLayers,
	};
}

void vulkan_bound_image_destroy(context *ctx, vulkan_bound_image *bnd)
{
	vulkan_image_view_destroy(ctx, bnd->view);
	vkDestroyImage(ctx->device, bnd->handle, NULL);
	vkFreeMemory(ctx->device, bnd->mem, NULL);
}

