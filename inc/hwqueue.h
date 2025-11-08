#ifndef GALA_HWQUEUE_H
#define GALA_HWQUEUE_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "types.h"
#include "gpu.h"


typedef struct {
	VkQueue handle;
	u32 family_index;
	u32 queue_index;
} hw_queue;

hw_queue hw_queue_ref(context *ctx, u32 family_index);
VkCommandPool command_pool_create(VkDevice logical,
	hw_queue queue, VkCommandPoolCreateFlags flags);
void command_buffer_create(VkDevice logical, VkCommandPool pool,
	u32 cnt, VkCommandBuffer *cmd);

#endif /* GALA_HWQUEUE_H */

