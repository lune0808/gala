#include <stdlib.h>
#include "lifetime.h"
#include "util.h"
#include "sync.h"


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
	if (n_cmd > 0) {
		l.q = q;
		l.pool = command_pool_create(ctx->device, q, flags);
		char *mem = xmalloc(n_cmd * (sizeof(VkCommandBuffer) + sizeof(VkFence)));
		l.cmd = (void*) mem;
		l.wait = (void*) (mem + n_cmd * sizeof(VkCommandBuffer));
		command_buffer_create(ctx->device, l.pool, n_cmd, l.cmd);
		cpu_fence_create(ctx->device, n_cmd, l.wait,
				VK_FENCE_CREATE_SIGNALED_BIT);
	}
	l.n_cmd = n_cmd;
	l.i_cmd = 0;

	l.n_buf = 0;
	l.c_buf = 1 * sizeof(*l.buf);
	l.buf = xmalloc(l.c_buf);

	l.n_img = 0;
	l.c_img = 1 * sizeof(*l.img);
	l.img = xmalloc(l.c_img);

	l.n_sm = 0;
	l.c_sm = 1 * sizeof(*l.sm);
	l.sm = xmalloc(l.c_sm);

	return l;
}

void lifetime_fini(lifetime *l, context *ctx)
{
	if (l->n_cmd > 0) {
		cpu_fence_wait_all(ctx->device,
			l->n_cmd, l->wait, UINT64_MAX);
	}
	for (u32 i = 0; i < l->n_cmd; i++) {
		vkDestroyFence(ctx->device, l->wait[i], NULL);
	}

	for (u32 i = 0; i < l->n_buf; i++) {
		vkDestroyBuffer(ctx->device, l->buf[i].handle, NULL);
		vkFreeMemory(ctx->device, l->buf[i].mem, NULL);
	}
	free(l->buf);
	for (u32 i = 0; i < l->n_img; i++) {
		vulkan_bound_image_destroy(ctx, &l->img[i]);
	}
	free(l->img);
	for (u32 i = 0; i < l->n_sm; i++) {
		vkDestroySampler(ctx->device, l->sm[i], NULL);
	}
	free(l->sm);

	if (l->n_cmd > 0) {
		free(l->cmd);
		vkDestroyCommandPool(ctx->device, l->pool, NULL);
	}
}

u32 lifetime_acquire(lifetime *l, context *ctx)
{
	VkFence fence = l->wait[l->i_cmd];
	cpu_fence_wait_one(ctx->device, fence, UINT64_MAX);
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

void lifetime_bind_image(lifetime *l, vulkan_bound_image img)
{
	buffer_fit((void**) &l->img, l->n_img * sizeof(*l->img), &l->c_img);
	l->img[l->n_img++] = img;
}

void lifetime_bind_sampler(lifetime *l, VkSampler sm)
{
	buffer_fit((void**) &l->sm, l->n_sm * sizeof(*l->sm), &l->c_sm);
	l->sm[l->n_sm++] = sm;
}

