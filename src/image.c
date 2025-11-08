#include <math.h>
#include <assert.h>
#include <string.h>
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

static void image_barrier(VkCommandBuffer cmd, VkImageMemoryBarrier *barrier,
	VkPipelineStageFlags pre, VkPipelineStageFlags post)
{
	vkCmdPipelineBarrier(cmd, pre, post, 0 /* TODO: VK_DEPENDENCY_BY_REGION_BIT */,
		0, NULL, 0, NULL, 1, barrier);
}

void vulkan_bound_image_layout_transition(VkCommandBuffer cmd, vulkan_bound_image *img,
	VkImageLayout prev, VkImageLayout next)
{
	VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.oldLayout = prev,
		.newLayout = next,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = img->handle,
		.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.subresourceRange.baseMipLevel = 0,
		.subresourceRange.levelCount = img->mips,
		.subresourceRange.baseArrayLayer = 0,
		.subresourceRange.layerCount = img->n_img,
	};
	VkPipelineStageFlags rel_stg, acq_stg;
	if (prev == VK_IMAGE_LAYOUT_UNDEFINED) {
		rel_stg = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		barrier.srcAccessMask = 0;
	} else if (prev == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		rel_stg = VK_PIPELINE_STAGE_TRANSFER_BIT;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	} else {
		crash("unimplemented image transition source");
	}
	if (next == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		acq_stg = VK_PIPELINE_STAGE_TRANSFER_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	} else if (next == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		acq_stg = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	} else {
		crash("unimplemented image transition destination");
	}
	image_barrier(cmd, &barrier, rel_stg, acq_stg);
}

void vulkan_bound_image_transfer(VkCommandBuffer cmd, vulkan_buffer buf, vulkan_bound_image *img)
{
	VkBufferImageCopy region = {
		.bufferOffset = 0,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.imageSubresource.mipLevel = 0,
		.imageSubresource.baseArrayLayer = 0,
		.imageSubresource.layerCount = img->n_img,
		.imageOffset = {0, 0, 0},
		.imageExtent = {img->dim.width, img->dim.height, 1},
	};
	vkCmdCopyBufferToImage(cmd, buf.handle, img->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void vulkan_bound_image_mips_transition(VkCommandBuffer cmd, vulkan_bound_image *img)
{
	VkImageMemoryBarrier pre_blit = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.image = img->handle,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.subresourceRange.baseArrayLayer = 0,
		.subresourceRange.layerCount = img->n_img,
		.subresourceRange.levelCount = 1,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	};
	VkImageMemoryBarrier post_blit = pre_blit;
	post_blit.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	post_blit.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	post_blit.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	post_blit.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkImageBlit blit_desc = {
		.srcOffsets[0] = { 0, 0, 0 },
		.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.srcSubresource.baseArrayLayer = 0,
		.srcSubresource.layerCount = img->n_img,
		.dstOffsets[0] = { 0, 0, 0 },
		.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.dstSubresource.baseArrayLayer = 0,
		.dstSubresource.layerCount = img->n_img,
	};
	i32 mip_w = (i32) img->dim.width;
	i32 mip_h = (i32) img->dim.height;
	for (u32 mip = 0; mip < img->mips-1; mip++) {
		// transition mip into src layout
		pre_blit.subresourceRange.baseMipLevel = mip;
		image_barrier(cmd, &pre_blit,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT);
		// blit mip into next mip
		i32 new_mip_w = (mip_w > 1)? mip_w/2: 1;
		i32 new_mip_h = (mip_h > 1)? mip_h/2: 1;
		blit_desc.srcOffsets[1] = (VkOffset3D){ mip_w, mip_h, 1 };
		blit_desc.srcSubresource.mipLevel = mip;
		blit_desc.dstOffsets[1] = (VkOffset3D){ new_mip_w, new_mip_h, 1 };
		blit_desc.dstSubresource.mipLevel = mip + 1;
		vkCmdBlitImage(cmd,
			img->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			img->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit_desc, VK_FILTER_LINEAR);
		// wait and transition mip into final state
		post_blit.subresourceRange.baseMipLevel = mip;
		image_barrier(cmd, &post_blit,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		mip_w = new_mip_w;
		mip_h = new_mip_h;
	}
	// transition last mip into final state
	post_blit.subresourceRange.baseMipLevel = img->mips - 1;
	post_blit.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	image_barrier(cmd, &post_blit,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

vulkan_bound_image vulkan_bound_image_upload(context *ctx, u32 n_img, loaded_image *img,
	hw_queue xfer)
{
	u32 width = img->width;
	u32 height = img->height;
	VkDeviceSize img_size = width * height * 4ul;
	VkDeviceSize size = img_size * (VkDeviceSize) n_img;
	vulkan_buffer staging = buffer_create(ctx,
		size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
		| VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	void *mapped = buffer_map(ctx, staging);
	for (u32 i = 0; i < n_img; i++) {
		assert(img[i].width  == width && img[i].height == height);
		memcpy((char*) mapped + i * img_size, img[i].mem, img_size);
		loaded_image_fini(img[i]);
	}
	buffer_unmap(ctx, staging);
	VkImageCreateInfo vimg_desc = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_SRGB,
		.extent = { img->width, img->height, 1 },
		.mipLevels = mips_for(img->width, img->height),
		.arrayLayers = n_img,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT
		       | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
		       | VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	vulkan_bound_image vimg = vulkan_bound_image_create(ctx,
		&vimg_desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT);
	VkCommandPool pool = command_pool_create(ctx->device,
		xfer, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
	VkCommandBuffer cmd;
	command_buffer_create(ctx->device, pool, 1, &cmd);
	VkCommandBufferBeginInfo cmd_begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(cmd, &cmd_begin);
	vulkan_bound_image_layout_transition(cmd, &vimg,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	VkFormatProperties props;
	vkGetPhysicalDeviceFormatProperties(
		ctx->physical_device, vimg.fmt, &props);
	if (!(props.optimalTilingFeatures
	    & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
		crash("vkCmdBlitImage not available for mipmap generation");
	vulkan_bound_image_transfer(cmd, staging, &vimg);
	vulkan_bound_image_mips_transition(cmd, &vimg);
	vkEndCommandBuffer(cmd);
	VkSubmitInfo submission = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd,
	};
	vkQueueSubmit(xfer.handle, 1, &submission, VK_NULL_HANDLE);
	vkQueueWaitIdle(xfer.handle);
	vkDestroyBuffer(ctx->device, staging.handle, NULL);
	vkFreeMemory(ctx->device, staging.mem, NULL);
	vkDestroyCommandPool(ctx->device, pool, NULL);
	return vimg;
}

