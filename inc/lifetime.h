#ifndef GALA_LIFETIME_H
#define GALA_LIFETIME_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "types.h"
#include "gpu.h"
#include "hwqueue.h"
#include "memory.h"
#include "image.h"


typedef struct lifetime {
	hw_queue q;
	VkCommandPool pool;
	VkCommandBuffer *cmd;
	VkFence *wait;
	u32 n_cmd;
	u32 i_cmd;

	vulkan_buffer *buf;
	u32 n_buf;
	u32 c_buf;

	vulkan_bound_image *img;
	u32 n_img;
	u32 c_img;

	VkSampler *sm;
	u32 n_sm;
	u32 c_sm;
} lifetime;

lifetime lifetime_init(context *ctx, hw_queue q,
	VkCommandPoolCreateFlags flags, u32 n_cmd);
u32 lifetime_acquire(lifetime *l, context *ctx);
void lifetime_release(lifetime *l, u32 icmd);
void lifetime_bind_buffer(lifetime *l, vulkan_buffer buf);
void lifetime_bind_image(lifetime *l, vulkan_bound_image img);
void lifetime_bind_sampler(lifetime *l, VkSampler sm);
void lifetime_fini(lifetime *l, context *ctx);

#endif /* GALA_LIFETIME_H */

