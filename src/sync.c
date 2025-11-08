#include <assert.h>
#include "sync.h"
#include "util.h"


void gpu_fence_create(VkDevice device, u32 cnt, VkSemaphore *sem)
{
	VkSemaphoreCreateInfo sem_desc = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};
	for (u32 i = 0; i < cnt; i++) {
		if (vkCreateSemaphore(device, &sem_desc,
			NULL, &sem[i]) != VK_SUCCESS)
			crash("vkCreateSemaphore");
	}
}

void cpu_fence_create(VkDevice device, u32 cnt, VkFence *fence,
	VkFenceCreateFlags flags)
{
	VkFenceCreateInfo fence_desc = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = flags,
	};
	for (u32 i = 0; i < cnt; i++) {
		if (vkCreateFence(device, &fence_desc,
			NULL, &fence[i]) != VK_SUCCESS)
			crash("vkCreateSemaphore");
	}
}

bool cpu_fence_wait_one(VkDevice device,
	VkFence fence, u64 timeout_ns)
{
	return cpu_fence_wait_all(device, 1, &fence, timeout_ns);
}

bool cpu_fence_wait_all(VkDevice device,
	u32 n_fence, VkFence *fence, u64 timeout_ns)
{
	VkResult result = vkWaitForFences(device,
		n_fence, fence, VK_TRUE, timeout_ns);
	if (result == VK_SUCCESS) {
		if (vkResetFences(device, n_fence, fence) != VK_SUCCESS)
			crash("vkResetFences");
		return true;
	} else if (result == VK_TIMEOUT) {
		return false;
	} else {
		crash("vkWaitForFences");
	}
}

u32 cpu_fence_wait_any(VkDevice device,
	u32 n_fence, VkFence *fence, u64 timeout_ns)
{
	VkResult result = vkWaitForFences(device,
		n_fence, fence, VK_FALSE, timeout_ns);
	if (result == VK_SUCCESS) {
		assert(n_fence < 32);
		u32 mask = 0;
		for (u32 i = 0; i < n_fence; i++) {
			if (vkGetFenceStatus(device, fence[i]) == VK_SUCCESS) {
				vkResetFences(device, 1, &fence[i]);
				mask |= (1ull << i);
			}
		}
		return mask;
	} else if (result == VK_TIMEOUT) {
		return false;
	} else {
		crash("vkWaitForFences");
	}
}

