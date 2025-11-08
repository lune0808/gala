#ifndef GALA_MEMORY_H
#define GALA_MEMORY_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "gpu.h"
struct lifetime; // circular dependency


u32 constrain_memory_type(context *ctx, u32 allowed,
	VkMemoryPropertyFlags cons);

typedef struct vulkan_buffer {
	VkBuffer handle;
	VkDeviceMemory mem;
	VkDeviceSize size;
} vulkan_buffer;

vulkan_buffer buffer_create(context *ctx,
	VkDeviceSize size, VkBufferUsageFlags usage,
	VkMemoryPropertyFlags cons);

void *buffer_map(context *ctx, vulkan_buffer buf);
void buffer_unmap(context *ctx, vulkan_buffer buf);
vulkan_buffer data_upload(context *ctx, VkDeviceSize size, const void *data,
	struct lifetime *l, VkBufferUsageFlags usage);

#endif /* GALA_MEMORY_H */

