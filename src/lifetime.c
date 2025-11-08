#include <stdlib.h>
#include "lifetime.h"
#include "util.h"


static void buffer_fit(void **mem, u32 size, u32 *cap)
{
	if (size >= *cap) {
		*mem = xrealloc(*mem, *cap *= 2);
	}
}

lifetime lifetime_init(context *ctx, hw_queue q,
	VkCommandPoolCreateFlags flags, u32 n_cmd)
{
	lifetime l;
	l.q = q;
	l.pool = command_pool_create(ctx->device, q, flags);
	char *mem = xmalloc(n_cmd * (sizeof(VkCommandBuffer) + sizeof(VkFence)));
	l.cmd = (void*) mem;
	l.wait = (void*) (mem + n_cmd * sizeof(VkCommandBuffer));
	command_buffer_create(ctx->device, l.pool, n_cmd, l.cmd);
	extern void cpu_fence_create(VkDevice device, u32 cnt, VkFence *fence,
		VkFenceCreateFlags flags);
	cpu_fence_create(ctx->device, n_cmd, l.wait,
		VK_FENCE_CREATE_SIGNALED_BIT);
	l.n_cmd = n_cmd;
	l.i_cmd = 0;

	l.n_buf = 0;
	l.c_buf = 1 * sizeof(*l.buf);
	l.buf = xmalloc(l.c_buf * sizeof(*l.buf));
	return l;
}

void lifetime_fini(lifetime *l, context *ctx)
{
	for (u32 i = 0; i < l->n_cmd; i++) {
		u32 icmd = lifetime_acquire(l, ctx);
		vkDestroyFence(ctx->device, l->wait[icmd], NULL);
	}
	free(l->cmd);
	for (u32 i = 0; i < l->n_buf; i++) {
		vkDestroyBuffer(ctx->device, l->buf[i].handle, NULL);
		vkFreeMemory(ctx->device, l->buf[i].mem, NULL);
	}
	free(l->buf);
	vkDestroyCommandPool(ctx->device, l->pool, NULL);
}

u32 lifetime_acquire(lifetime *l, context *ctx)
{
	VkFence fence = l->wait[l->i_cmd];
	if (vkWaitForFences(ctx->device,
		1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
		crash("vkWaitForFences");
	if (vkResetFences(ctx->device, 1, &fence) != VK_SUCCESS)
		crash("vkResetFences");
	VkCommandBuffer cmd = l->cmd[l->i_cmd];
	vkResetCommandBuffer(cmd, 0);
	u32 icmd = l->i_cmd;
	l->i_cmd = (l->i_cmd + 1) % l->n_cmd;
	return icmd;
}

void lifetime_release(lifetime *l, u32 icmd)
{
	VkSubmitInfo submission = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &l->cmd[icmd],
	};
	vkQueueSubmit(l->q.handle, 1, &submission, l->wait[icmd]);
}

void lifetime_bind_buffer(lifetime *l, vulkan_buffer buf)
{
	buffer_fit((void**) &l->buf, l->n_buf * sizeof(*l->buf), &l->c_buf);
	l->buf[l->n_buf++] = buf;
}

