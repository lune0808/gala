#include "hwqueue.h"
#include "util.h"


hw_queue hw_queue_ref(context *ctx, u32 family_index)
{
	VkQueue handle;
	u32 queue_index = 0;
	vkGetDeviceQueue(ctx->device, family_index, queue_index, &handle);
	return (hw_queue){ handle, family_index, queue_index };
}

VkCommandPool command_pool_create(VkDevice logical,
	hw_queue queue, VkCommandPoolCreateFlags flags)
{
	VkCommandPoolCreateInfo pool_desc = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = flags,
		.queueFamilyIndex = queue.family_index,
	};
	VkCommandPool pool;
	if (vkCreateCommandPool(logical, &pool_desc, NULL, &pool) != VK_SUCCESS)
		crash("vkCreateCommandPool");
	return pool;
}

void command_buffer_create(VkDevice logical, VkCommandPool pool,
	u32 cnt, VkCommandBuffer *cmd)
{
	VkCommandBufferAllocateInfo buf_desc = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = cnt,
	};
	if (vkAllocateCommandBuffers(logical, &buf_desc, cmd) != VK_SUCCESS)
		crash("vkCreateCommandBuffers");
}
