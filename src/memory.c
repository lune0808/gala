#include <string.h>
#include "memory.h"
#include "util.h"
#include "lifetime.h"


u32 constrain_memory_type(context *ctx, u32 allowed, VkMemoryPropertyFlags cons)
{
	VkPhysicalDeviceMemoryProperties *props = &ctx->specs->memory;
	for (u32 i = 0; i < props->memoryTypeCount; i++) {
		if (allowed & (1u << i)) {
			if ((props->memoryTypes[i].propertyFlags & cons) == cons) {
				return i;
			}
		}
	}
	crash("no suitable memory type available");
}

vulkan_buffer buffer_create(context *ctx,
	VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags cons)
{
	VkBufferCreateInfo buf_desc = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	VkBuffer buf;
	if (vkCreateBuffer(ctx->device, &buf_desc, NULL, &buf) != VK_SUCCESS)
		crash("vkCreateBuffer");
	VkMemoryRequirements reqs;
	vkGetBufferMemoryRequirements(ctx->device, buf, &reqs);
	VkMemoryAllocateInfo mem_desc = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = reqs.size,
		.memoryTypeIndex = constrain_memory_type(
			ctx,
			reqs.memoryTypeBits,
			cons
		),
	};
	VkDeviceMemory mem;
	if (vkAllocateMemory(ctx->device, &mem_desc, NULL, &mem) != VK_SUCCESS)
		crash("vkAllocateMemory");
	vkBindBufferMemory(ctx->device, buf, mem, 0);
	return (vulkan_buffer){ buf, mem, size };
}

void *buffer_map(context *ctx, vulkan_buffer buf)
{
	void *mapped;
	vkMapMemory(ctx->device, buf.mem, 0, buf.size, 0, &mapped);
	return mapped;
}

void buffer_unmap(context *ctx, vulkan_buffer buf)
{
	vkUnmapMemory(ctx->device, buf.mem);
}

static void data_transfer(context *ctx, vulkan_buffer dst, vulkan_buffer src,
	lifetime *l)
{
	u32 icmd = lifetime_acquire(l, ctx);
	VkCommandBuffer cmd = l->cmd[icmd];
	VkCommandBufferBeginInfo cmd_begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(cmd, &cmd_begin);
	VkBufferCopy copy_desc = {
		.srcOffset = 0,
		.dstOffset = 0,
		.size = dst.size,
	};
	vkCmdCopyBuffer(cmd, src.handle, dst.handle, 1, &copy_desc);
	vkEndCommandBuffer(cmd);
	lifetime_release(l, icmd);
}

vulkan_buffer data_upload(context *ctx, VkDeviceSize size, const void *data,
	lifetime *l, VkBufferUsageFlags usage)
{
	vulkan_buffer staging = buffer_create(ctx,
		size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	memcpy(buffer_map(ctx, staging), data, size);
	buffer_unmap(ctx, staging);
	vulkan_buffer uploaded = buffer_create(ctx,
		size, VK_BUFFER_USAGE_TRANSFER_DST_BIT|usage,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	data_transfer(ctx, uploaded, staging, l);
	lifetime_bind_buffer(l, staging);
	return uploaded;
}

