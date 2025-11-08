#ifndef GALA_IMAGE_H
#define GALA_IMAGE_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "types.h"
#include "gpu.h"

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


#endif /* GALA_IMAGE_H */

