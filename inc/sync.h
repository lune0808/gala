#ifndef GALA_SYNC_H
#define GALA_SYNC_H


#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "types.h"
#include <stdbool.h>

void gpu_fence_create(VkDevice device, u32 cnt, VkSemaphore *sem);
void cpu_fence_create(VkDevice device, u32 cnt, VkFence *fence,
	VkFenceCreateFlags flags);
bool cpu_fence_wait_one(VkDevice device,
	VkFence fence, u64 timeout_ns);
bool cpu_fence_wait_all(VkDevice device,
	u32 n_fence, VkFence *fence, u64 timeout_ns);
u32 cpu_fence_wait_any(VkDevice device,
	u32 n_fence, VkFence *fence, u64 timeout_ns);

#endif /* GALA_SYNC_H */

