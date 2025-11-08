#ifndef GALA_IMAGE_H
#define GALA_IMAGE_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "types.h"
#include "gpu.h"
#include "memory.h"
struct lifetime;

typedef struct {
	u32 width;
	u32 height;
	void *mem;
} loaded_image;

loaded_image load_image(const char *path);
void loaded_image_fini(loaded_image img);
u32 mips_for(u32 width, u32 height);

VkImageView vulkan_image_view_create_external(context *ctx, VkImage handle,
	VkFormat fmt, u32 mips, u32 n_img, VkImageAspectFlags kind);
void vulkan_image_view_destroy(context *ctx, VkImageView view);

typedef struct {
	VkImage handle;
	VkImageView view;
	VkDeviceMemory mem;
	VkExtent2D dim;
	VkFormat fmt;
	u32 mips;
	u32 n_img;
} vulkan_bound_image;

vulkan_bound_image vulkan_bound_image_create(context *ctx,
	VkImageCreateInfo *desc, VkMemoryPropertyFlags memory, VkImageAspectFlags kind);
void vulkan_bound_image_destroy(context *ctx, vulkan_bound_image *bnd);
void vulkan_bound_image_layout_transition(VkCommandBuffer cmd, vulkan_bound_image *img,
	VkImageLayout prev, VkImageLayout next);
void vulkan_bound_image_transfer(VkCommandBuffer cmd,
	vulkan_buffer buf, vulkan_bound_image *img);
void vulkan_bound_image_mips_transition(VkCommandBuffer cmd,
	vulkan_bound_image *img);
vulkan_bound_image vulkan_bound_image_upload(context *ctx,
	u32 n_img, loaded_image *img, struct lifetime *l);

#endif /* GALA_IMAGE_H */

