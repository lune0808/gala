#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>
#include <stb/stb_image.h>
#include "shared.h"
#include "util.h"
#include "types.h"
#include "gpu.h"

#define MAX_FRAMES_RENDERING (2)

VkExtent2D swapchain_select_resolution(context *ctx, VkSurfaceCapabilitiesKHR *pcap)
{
	VkSurfaceCapabilitiesKHR cap;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physical_device,
		ctx->window_surface, &cap);
	*pcap = cap;
	if (cap.currentExtent.width == UINT32_MAX) {
		int width, height;
		glfwGetFramebufferSize(ctx->window, &width, &height);
		return (VkExtent2D){
			CLAMP((u32) width, cap.minImageExtent.width, cap.maxImageExtent.height),
			CLAMP((u32) height, cap.minImageExtent.height, cap.maxImageExtent.height),
		};
	} else {
		return cap.currentExtent;
	}
}

VkImageView image_view_create(VkDevice logical, VkImage img, VkFormat fmt, VkImageAspectFlags kind, u32 mips)
{
	VkImageViewCreateInfo view_desc = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = img,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = fmt,
		.subresourceRange.aspectMask = kind,
		.subresourceRange.baseMipLevel = 0,
		.subresourceRange.levelCount = mips,
		.subresourceRange.baseArrayLayer = 0,
		.subresourceRange.layerCount = 1,
	};
	VkImageView view;
	if (vkCreateImageView(logical, &view_desc, NULL, &view) != VK_SUCCESS)
		crash("vkCreateImageView");
	return view;
}

VkRenderPass render_pass_create_or_crash(VkDevice logical, VkFormat fmt,
	VkFormat depth_fmt)
{
	VkAttachmentDescription attacht[] = {
		{
			.format = fmt,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		},
		{
			.format = depth_fmt,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		},
	};
	VkAttachmentReference color_attacht_ref = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	VkAttachmentReference depth_attacht_ref = {
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	};
	VkSubpassDescription subpass_desc = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &color_attacht_ref,
		.pDepthStencilAttachment = &depth_attacht_ref,
	};
	VkSubpassDependency draw_dep = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.srcStageMask =
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		      | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.dstSubpass = 0,
		.dstStageMask =
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		      | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
		.dstAccessMask =
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
		      | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
	};
	VkRenderPassCreateInfo pass_desc = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = ARRAY_SIZE(attacht),
		.pAttachments = attacht,
		.subpassCount = 1,
		.pSubpasses = &subpass_desc,
		.dependencyCount = 1,
		.pDependencies = &draw_dep,
	};
	VkRenderPass pass;
	if (vkCreateRenderPass(logical, &pass_desc, NULL, &pass) != VK_SUCCESS)
		crash("vkCreateRenderPass");
	return pass;
}

typedef struct {
	VkSwapchainKHR chain;
	VkImage *slot;
	VkImageView *view;
	u32 n_slot;
	u32 current;
	VkFormat fmt;
	VkExtent2D dim;
} swapchain;

swapchain swapchain_create(context *ctx)
{
	VkSurfaceKHR surface = ctx->window_surface;
	VkSurfaceCapabilitiesKHR cap;
	VkExtent2D dim = swapchain_select_resolution(ctx, &cap);
	VkPresentModeKHR mode = ctx->present_mode;
	VkSurfaceFormatKHR fmt = ctx->present_surface_fmt;
	u32 expected_swap_cnt = cap.minImageCount + 1u;
	VkSwapchainCreateInfoKHR swap_desc = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = (cap.maxImageCount != 0)?
			MIN(cap.maxImageCount, expected_swap_cnt):
			expected_swap_cnt,
		.imageFormat = fmt.format,
		.imageColorSpace = fmt.colorSpace,
		.imageExtent = dim,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = cap.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = mode,
		.clipped = VK_TRUE,
		.oldSwapchain = VK_NULL_HANDLE,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	swapchain swap;
	if (vkCreateSwapchainKHR(ctx->device,
		&swap_desc, NULL, &swap.chain) != VK_SUCCESS)
		crash("vkCreateSwapchainKHR");
	vkGetSwapchainImagesKHR(ctx->device, swap.chain, &swap.n_slot, NULL);
	swap.slot = xmalloc(swap.n_slot * sizeof *swap.slot);
	vkGetSwapchainImagesKHR(ctx->device, swap.chain, &swap.n_slot, swap.slot);
	swap.fmt = fmt.format;
	swap.dim = dim;
	swap.view = xmalloc(swap.n_slot * sizeof *swap.view);
	for (u32 i = 0; i < swap.n_slot; i++) {
		swap.view[i] = image_view_create(ctx->device, swap.slot[i],
			fmt.format, VK_IMAGE_ASPECT_COLOR_BIT, 1u);
	}
	return swap;
}

VkFormat format_supported(VkPhysicalDevice physical, size_t n_among, VkFormat *among, VkImageTiling tiling, VkFormatFeatureFlags cons)
{
	size_t features_offset;
	switch (tiling) {
	case VK_IMAGE_TILING_LINEAR:
		features_offset = offsetof(VkFormatProperties, linearTilingFeatures);
		break;
	case VK_IMAGE_TILING_OPTIMAL:
		features_offset = offsetof(VkFormatProperties, optimalTilingFeatures);
		break;
	default:
		assert(0);
	}
	for (size_t i = 0; i < n_among; i++) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(physical, among[i], &props);
		VkFormatFeatureFlags *features = (VkFormatFeatureFlags*) ((char*) &props + features_offset);
		if ((*features & cons) == cons) {
			return among[i];
		}
	}
	return VK_FORMAT_UNDEFINED;
}

bool format_has_stencil(VkFormat fmt)
{
	return fmt == VK_FORMAT_D32_SFLOAT_S8_UINT
	    || fmt == VK_FORMAT_D24_UNORM_S8_UINT;
}

VkFramebuffer *framebuf_attach_or_crash(VkDevice logical, swapchain swap, VkRenderPass pass, VkImageView depth_view)
{
	VkFramebuffer *framebuf = xmalloc(swap.n_slot * sizeof *framebuf);
	for (u32 i = 0; i < swap.n_slot; i++) {
		VkImageView attacht[] = {
			swap.view[i],
			depth_view,
		};
		VkFramebufferCreateInfo framebuf_desc = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = pass,
			.attachmentCount = ARRAY_SIZE(attacht),
			.pAttachments = attacht,
			.width = swap.dim.width,
			.height = swap.dim.height,
			.layers = 1,
		};
		if (vkCreateFramebuffer(logical, &framebuf_desc, NULL, &framebuf[i]) != VK_SUCCESS)
			crash("vkCreateFramebuffer");
	}
	return framebuf;
}

typedef struct {
	void *mem;
	size_t size;
} buffer;

buffer load_file_or_crash(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) goto fail_open;
	long status;
	status = fseek(f, 0, SEEK_END);
	if (status != 0) goto io;
	status = ftell(f);
	if (status < 0) goto io;
	size_t sz = (size_t) status;
	status = fseek(f, 0, SEEK_SET);
	if (status != 0) goto io;
	void *buf = malloc(sz);
	if (!buf) goto io;
	size_t remaining_sz = sz;
	for (void *cur = buf; !feof(f);) {
		size_t amount_read = fread(cur, remaining_sz, 1, f);
		cur += amount_read;
		remaining_sz -= amount_read;
		if (ferror(f)) goto alloc;
	}
	fclose(f);
	return (buffer){ buf, sz };
alloc:
	free(buf);
io:
	fclose(f);
fail_open:
	crash("load file '%s' failed");
}

VkShaderModule build_shader_module_or_crash(const char *path, VkDevice logical)
{
	buffer buf = load_file_or_crash(path);
	VkShaderModuleCreateInfo desc = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = buf.size,
		.pCode = buf.mem,
	};
	VkShaderModule sh;
	if (vkCreateShaderModule(logical, &desc, NULL, &sh) != VK_SUCCESS)
		crash("build shader %s failed", path);
	free(buf.mem);
	return sh;
}

VkDescriptorSetLayout descriptor_set_lyt_create_or_crash(VkDevice logical,
	u32 n_bind_desc, VkDescriptorSetLayoutBinding *bind_desc)
{
	VkDescriptorSetLayoutCreateInfo lyt_desc = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = n_bind_desc,
		.pBindings = bind_desc,
	};
	VkDescriptorSetLayout lyt;
	if (vkCreateDescriptorSetLayout(logical, &lyt_desc, NULL, &lyt) != VK_SUCCESS)
		crash("vkCreateDescriptorSetLayout");
	return lyt;
}

VkDescriptorPool descr_pool_create_or_crash(VkDevice logical,
	u32 n_pool_sizes, VkDescriptorPoolSize *pool_sizes)
{
	VkDescriptorPoolCreateInfo pool_desc = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = n_pool_sizes,
		.pPoolSizes = pool_sizes,
		.maxSets = (u32) MAX_FRAMES_RENDERING,
	};
	VkDescriptorPool pool;
	if (vkCreateDescriptorPool(logical, &pool_desc, NULL, &pool) != VK_SUCCESS)
		crash("vkCreateDescriptorPool");
	return pool;
}

VkDescriptorSet *descr_set_create_or_crash(VkDevice logical,
	VkDescriptorPool pool, VkDescriptorSetLayout lyt)
{
	VkDescriptorSetLayout lyt_dupes[MAX_FRAMES_RENDERING];
	for (u32 i = 0; i < MAX_FRAMES_RENDERING; i++) {
		lyt_dupes[i] = lyt;
	}
	VkDescriptorSet *set = malloc(MAX_FRAMES_RENDERING * sizeof *set);
	VkDescriptorSetAllocateInfo alloc_desc = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = pool,
		.descriptorSetCount = ARRAY_SIZE(lyt_dupes),
		.pSetLayouts = lyt_dupes,
	};
	if (vkAllocateDescriptorSets(logical, &alloc_desc, set) != VK_SUCCESS)
		crash("vkAllocateDescriptorSets");
	return set;
}

u32 constrain_memory_type_or_crash(context *ctx, u32 allowed, VkMemoryPropertyFlags cons)
{
	VkPhysicalDeviceMemoryProperties *props = &ctx->specs->memory;
	for (u32 i = 0; i < props->memoryTypeCount; i++) {
		if (allowed & (1u << i)) {
			if ((props->memoryTypes[i].propertyFlags & cons) == cons) {
				return i;
			}
		}
	}
	crash("no suitable memory type available");
}

typedef struct {
	VkBuffer buf;
	VkDeviceMemory mem;
	VkDeviceSize size;
} vulkan_buffer;

vulkan_buffer buffer_create_or_crash(context *ctx,
	VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags cons)
{
	VkBufferCreateInfo buf_desc = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	VkBuffer buf;
	if (vkCreateBuffer(ctx->device, &buf_desc, NULL, &buf) != VK_SUCCESS)
		crash("vkCreateBuffer");
	VkMemoryRequirements reqs;
	vkGetBufferMemoryRequirements(ctx->device, buf, &reqs);
	VkMemoryAllocateInfo mem_desc = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = reqs.size,
		.memoryTypeIndex = constrain_memory_type_or_crash(
			ctx,
			reqs.memoryTypeBits,
			cons
		),
	};
	VkDeviceMemory mem;
	if (vkAllocateMemory(ctx->device, &mem_desc, NULL, &mem) != VK_SUCCESS)
		crash("vkAllocateMemory");
	vkBindBufferMemory(ctx->device, buf, mem, 0);
	return (vulkan_buffer){ buf, mem, size };
}

void descr_set_config(VkDevice logical, VkDescriptorSet *set, vulkan_buffer buf,
	VkImageView tex_view, VkSampler tex_sm)
{
	VkDescriptorBufferInfo buf_desc = {
		.buffer = buf.buf,
		.offset = 0,
		.range = sizeof(transforms),
	};
	VkDescriptorImageInfo tex_desc = {
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.imageView = tex_view,
		.sampler = tex_sm,
	};
	VkWriteDescriptorSet write_desc[] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.pBufferInfo = &buf_desc,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstBinding = 1,
			.dstArrayElement = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.pImageInfo = &tex_desc,
		},
	};
	for (u32 i = 0; i < MAX_FRAMES_RENDERING; i++) {
		write_desc[0].dstSet = set[i];
		write_desc[1].dstSet = set[i];
		vkUpdateDescriptorSets(logical,
			ARRAY_SIZE(write_desc), write_desc, 0, NULL);
		buf_desc.offset += sizeof(transforms);
	}
}

typedef struct {
	VkPipeline line;
	VkPipelineLayout layout;
	VkDescriptorSetLayout set_layout;
	VkDescriptorPool dpool;
	VkDescriptorSet *set;
} pipeline;

typedef struct {
	vec3 pos;
	vec3 col;
	vec2 uv;
} vertex;

static const vertex vertices[] = {
	{ { -0.5f, -0.5f, +0.15f }, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f} },
	{ { +0.5f, -0.5f, +0.15f }, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} },
	{ { +0.5f, +0.5f, +0.15f }, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f} },
	{ { -0.5f, +0.5f, +0.15f }, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f} },
	{ { -0.5f, -0.5f, -0.15f }, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f} },
	{ { +0.5f, -0.5f, -0.15f }, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} },
	{ { +0.5f, +0.5f, -0.15f }, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f} },
	{ { -0.5f, +0.5f, -0.15f }, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f} },
};

static const u16 indices[] = {
	0, 1, 2,
	2, 3, 0,
	4, 5, 6,
	6, 7, 4,
};

pipeline graphics_pipeline_create_or_crash(const char *vert_path, const char *frag_path,
	VkDevice logical, VkExtent2D dims, VkRenderPass gpass, vulkan_buffer ubuf,
	VkImageView tex_view, VkSampler tex_sm)
{
	VkShaderModule vert_mod = build_shader_module_or_crash(vert_path, logical);
	VkShaderModule frag_mod = build_shader_module_or_crash(frag_path, logical);
	VkPipelineShaderStageCreateInfo stg_desc[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vert_mod,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = frag_mod,
			.pName = "main",
		}
	};
	VkPipelineDynamicStateCreateInfo dyn_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
	};
	VkVertexInputAttributeDescription attributes[] = {
		{
			.binding = 0,
			.location = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(vertex, pos),
		},
		{
			.binding = 0,
			.location = 1,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(vertex, col),
		},
		{
			.binding = 0,
			.location = 2,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = offsetof(vertex, uv),
		},
	};
	VkPipelineVertexInputStateCreateInfo vert_lyt_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &(VkVertexInputBindingDescription){
			.binding = 0,
			.stride = sizeof(vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		},
		.vertexAttributeDescriptionCount = ARRAY_SIZE(attributes),
		.pVertexAttributeDescriptions = attributes,
	};
	VkPipelineInputAssemblyStateCreateInfo ia_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE,
	};
	VkPipelineViewportStateCreateInfo vp_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = &(VkViewport){
			.x = 0.0f,
			.y = 0.0f,
			.width = (float) dims.width,
			.height = (float) dims.height,
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		},
		.scissorCount = 1,
		.pScissors = &(VkRect2D){
			.offset = {0, 0},
			.extent = dims,
		},
	};
	VkPipelineRasterizationStateCreateInfo ras_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.lineWidth = 1.0f,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.depthBiasEnable = VK_FALSE,
	};
	VkPipelineMultisampleStateCreateInfo ms_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.sampleShadingEnable = VK_FALSE,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};
	VkPipelineDepthStencilStateCreateInfo ds_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
		.depthBoundsTestEnable = VK_FALSE,
		.stencilTestEnable = VK_FALSE,
	};
	VkPipelineColorBlendAttachmentState blend_attach = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
				| VK_COLOR_COMPONENT_G_BIT
				| VK_COLOR_COMPONENT_B_BIT
				| VK_COLOR_COMPONENT_A_BIT,
		.blendEnable = VK_FALSE,
	};
	VkPipelineColorBlendStateCreateInfo blend_global = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = VK_FALSE,
		.attachmentCount = 1,
		.pAttachments = &blend_attach,
	};
	VkDescriptorSetLayoutBinding bind_desc[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		}
	};
	VkDescriptorSetLayout set_lyt = descriptor_set_lyt_create_or_crash(logical,
		ARRAY_SIZE(bind_desc), bind_desc);
	VkDescriptorPoolSize pool_sizes[] = {
		{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = (u32) MAX_FRAMES_RENDERING,
		},
		{
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = (u32) MAX_FRAMES_RENDERING,
		},
	};
	VkDescriptorPool dpool = descr_pool_create_or_crash(logical,
		ARRAY_SIZE(pool_sizes), pool_sizes);
	VkDescriptorSet *set = descr_set_create_or_crash(logical,
		dpool, set_lyt);
	descr_set_config(logical, set, ubuf, tex_view, tex_sm);
	VkPipelineLayoutCreateInfo unif_lyt_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &set_lyt,
	};
	VkPipelineLayout unif_lyt;
	if (vkCreatePipelineLayout(logical, &unif_lyt_desc, NULL, &unif_lyt) != VK_SUCCESS)
		crash("vkCreatePipelineLayout");

	VkGraphicsPipelineCreateInfo gpipe_desc = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = ARRAY_SIZE(stg_desc),
		.pStages = stg_desc,
		.pVertexInputState = &vert_lyt_desc,
		.pInputAssemblyState = &ia_desc,
		.pViewportState = &vp_desc,
		.pRasterizationState = &ras_desc,
		.pMultisampleState = &ms_desc,
		.pDepthStencilState = &ds_desc,
		.pColorBlendState = &blend_global,
		.pDynamicState = &dyn_desc,
		.layout = unif_lyt,
		.renderPass = gpass,
		.subpass = 0,
	};
	VkPipeline gpipe;
	if (vkCreateGraphicsPipelines(logical, VK_NULL_HANDLE, 1, &gpipe_desc, NULL, &gpipe) != VK_SUCCESS)
		crash("vkCreateGraphicsPipelines");

	vkDestroyShaderModule(logical, frag_mod, NULL);
	vkDestroyShaderModule(logical, vert_mod, NULL);
	return (pipeline){ gpipe, unif_lyt, set_lyt, dpool, set };
}

typedef struct {
	VkQueue handle;
	u32 family_index;
	u32 queue_index;
} vulkan_queue;

vulkan_queue vulkan_queue_ref(context *ctx, u32 family_index)
{
	VkQueue handle;
	u32 queue_index = 0;
	vkGetDeviceQueue(ctx->device, family_index, queue_index, &handle);
	return (vulkan_queue){ handle, family_index, queue_index };
}

VkCommandPool command_pool_create_or_crash(VkDevice logical,
	vulkan_queue queue, VkCommandPoolCreateFlags flags)
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

void buffer_populate(VkDevice logical, vulkan_buffer b, const void *data)
{
	void *mapped;
	vkMapMemory(logical, b.mem, 0, b.size, 0, &mapped);
	memcpy(mapped, data, b.size);
	vkUnmapMemory(logical, b.mem);
}

void command_buffer_create_or_crash(VkDevice logical, VkCommandPool pool,
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

void data_transfer(VkDevice logical, vulkan_buffer dst, vulkan_buffer src,
	vulkan_queue xfer)
{
	VkCommandPool pool = command_pool_create_or_crash(logical,
		xfer, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
	VkCommandBuffer cmd;
	command_buffer_create_or_crash(logical, pool, 1, &cmd);
	VkCommandBufferBeginInfo cmd_begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(cmd, &cmd_begin);
	VkBufferCopy copy_desc = {
		.srcOffset = 0,
		.dstOffset = 0,
		.size = dst.size,
	};
	vkCmdCopyBuffer(cmd, src.buf, dst.buf, 1, &copy_desc);
	vkEndCommandBuffer(cmd);
	VkSubmitInfo submission = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd,
	};
	vkQueueSubmit(xfer.handle, 1, &submission, VK_NULL_HANDLE);
	vkQueueWaitIdle(xfer.handle);
	vkFreeCommandBuffers(logical, pool, 1, &cmd);
	vkDestroyCommandPool(logical, pool, NULL);
}

vulkan_buffer data_upload(context *ctx, VkDeviceSize size, const void *data,
	vulkan_queue xfer, VkBufferUsageFlags usage)
{
	vulkan_buffer staging = buffer_create_or_crash(ctx,
		size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	buffer_populate(ctx->device, staging, data);
	vulkan_buffer uploaded = buffer_create_or_crash(ctx,
		size, VK_BUFFER_USAGE_TRANSFER_DST_BIT|usage,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	data_transfer(ctx->device, uploaded, staging, xfer);
	vkDestroyBuffer(ctx->device, staging.buf, NULL);
	vkFreeMemory(ctx->device, staging.mem, NULL);
	return uploaded;
}

typedef struct {
	int width;
	int height;
	void *mem;
} image;

image load_image(const char *path)
{
	int w, h, ch;
	void *ptr = stbi_load(path, &w, &h, &ch, 4);
	if (!ptr) crash("stbi_load");
	return (image){ w, h, ptr };
}

typedef struct {
	VkImage img;
	VkDeviceMemory mem;
	u32 mips;
} vulkan_image;

u32 mips_for(u32 width, u32 height)
{
	float max_dim = MAX((float) width, (float) height);
	u32 mips = 1u + (u32) floorf(log2f(max_dim));
	return mips;
}

vulkan_image vulkan_image_create(context *ctx,
	VkFormat fmt, u32 width, u32 height, VkImageUsageFlags usage,
	VkMemoryPropertyFlags prop, VkImageTiling tiling, u32 mips)
{
	VkImageCreateInfo img_desc = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.extent.width = width,
		.extent.height = height,
		.extent.depth = 1u,
		.mipLevels = mips,
		.arrayLayers = 1,
		.format = fmt,
		.tiling = tiling,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.samples = VK_SAMPLE_COUNT_1_BIT,
	};
	VkImage vulkan_img;
	if (vkCreateImage(ctx->device, &img_desc, NULL, &vulkan_img) != VK_SUCCESS)
		crash("vkCreateImage");

	VkMemoryRequirements reqs;
	vkGetImageMemoryRequirements(ctx->device, vulkan_img, &reqs);
	VkMemoryAllocateInfo alloc_desc = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = reqs.size,
		.memoryTypeIndex = constrain_memory_type_or_crash(
			ctx,
			reqs.memoryTypeBits,
			prop),
	};
	VkDeviceMemory mem;
	if (vkAllocateMemory(ctx->device, &alloc_desc, NULL, &mem) != VK_SUCCESS)
		crash("vkAllocateMemory");
	vkBindImageMemory(ctx->device, vulkan_img, mem, 0);
	return (vulkan_image){ vulkan_img, mem, mips };
}

void image_layout_transition(VkCommandBuffer cmd, VkImage img,
	VkImageLayout prev, VkImageLayout next, u32 mips)
{
	VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.oldLayout = prev,
		.newLayout = next,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = img,
		.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.subresourceRange.baseMipLevel = 0,
		.subresourceRange.levelCount = mips,
		.subresourceRange.baseArrayLayer = 0,
		.subresourceRange.layerCount = 1,
	};
	VkPipelineStageFlags rel_stg, acq_stg;
	if (prev == VK_IMAGE_LAYOUT_UNDEFINED) {
		rel_stg = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		barrier.srcAccessMask = 0;
	} else if (prev == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		rel_stg = VK_PIPELINE_STAGE_TRANSFER_BIT;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	} else {
		crash("unimplemented image transition source");
	}
	if (next == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		acq_stg = VK_PIPELINE_STAGE_TRANSFER_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	} else if (next == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		acq_stg = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	} else {
		crash("unimplemented image transition destination");
	}
	vkCmdPipelineBarrier(cmd,
		rel_stg, acq_stg, 0 /* TODO: VK_DEPENDENCY_BY_REGION_BIT */,
		0, NULL, 0, NULL, 1, &barrier);
}

void image_transfer(VkCommandBuffer cmd, vulkan_buffer buf, vulkan_image img,
	u32 width, u32 height)
{
	VkBufferImageCopy region = {
		.bufferOffset = 0,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.imageSubresource.mipLevel = 0,
		.imageSubresource.baseArrayLayer = 0,
		.imageSubresource.layerCount = 1,
		.imageOffset = {0, 0, 0},
		.imageExtent = {width, height, 1},
	};
	vkCmdCopyBufferToImage(cmd, buf.buf, img.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void image_mips_transition(VkCommandBuffer cmd, VkImage img, u32 width, u32 height,
	u32 mips)
{
	VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.image = img,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.subresourceRange.baseArrayLayer = 0,
		.subresourceRange.layerCount = 1,
		.subresourceRange.levelCount = 1,
	};
	VkImageBlit blit_desc = {
		.srcOffsets[0] = { 0, 0, 0 },
		.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.srcSubresource.baseArrayLayer = 0,
		.srcSubresource.layerCount = 1,
		.dstOffsets[0] = { 0, 0, 0 },
		.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.dstSubresource.baseArrayLayer = 0,
		.dstSubresource.layerCount = 1,
	};
	i32 mip_w = (i32) width;
	i32 mip_h = (i32) height;
	for (u32 mip = 0; mip < mips-1; mip++) {
		// transition mip into src layout
		barrier.subresourceRange.baseMipLevel = mip;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, NULL, 0, NULL, 1, &barrier
		);
		// blit mip into next mip
		i32 new_mip_w = (mip_w > 1)? mip_w/2: 1;
		i32 new_mip_h = (mip_h > 1)? mip_h/2: 1;
		blit_desc.srcOffsets[1] = (VkOffset3D){ mip_w, mip_h, 1 };
		blit_desc.srcSubresource.mipLevel = mip;
		blit_desc.dstOffsets[1] = (VkOffset3D){ new_mip_w, new_mip_h, 1 };
		blit_desc.dstSubresource.mipLevel = mip + 1;
		vkCmdBlitImage(cmd,
			img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit_desc, VK_FILTER_LINEAR);
		// wait and transition mip into final state
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, NULL, 0, NULL, 1, &barrier
		);
		mip_w = new_mip_w;
		mip_h = new_mip_h;
	}
	// transition last mip into final state
	barrier.subresourceRange.baseMipLevel = mips - 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(cmd,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0, 0, NULL, 0, NULL, 1, &barrier
	);
}

vulkan_image image_upload(context *ctx, image img, vulkan_queue xfer)
{
	VkDeviceSize size = (VkDeviceSize) img.width * (VkDeviceSize) img.height * 4ul;
	vulkan_buffer staging = buffer_create_or_crash(ctx,
		size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	buffer_populate(ctx->device, staging, img.mem);
	stbi_image_free(img.mem);
	VkFormat fmt = VK_FORMAT_R8G8B8A8_SRGB;
	vulkan_image vimg = vulkan_image_create(ctx,
		fmt, (u32) img.width, (u32) img.height,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT
	      | VK_IMAGE_USAGE_SAMPLED_BIT
	      | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_TILING_OPTIMAL,
		mips_for((u32) img.width, (u32) img.height));
	VkFormatProperties props;
	vkGetPhysicalDeviceFormatProperties(ctx->physical_device, fmt, &props);
	if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
		crash("vkCmdBlitImage not available for mipmap generation");
	VkCommandPool pool = command_pool_create_or_crash(ctx->device,
		xfer, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
	VkCommandBuffer cmd;
	command_buffer_create_or_crash(ctx->device, pool, 1, &cmd);
	VkCommandBufferBeginInfo cmd_begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(cmd, &cmd_begin);
	image_layout_transition(cmd, vimg.img,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vimg.mips);
	image_transfer(cmd, staging, vimg, (u32) img.width, (u32) img.height);
	image_mips_transition(cmd, vimg.img, (u32) img.width, (u32) img.height, vimg.mips);
	VkSubmitInfo submission = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd,
	};
	vkEndCommandBuffer(cmd);
	vkQueueSubmit(xfer.handle, 1, &submission, VK_NULL_HANDLE);
	vkQueueWaitIdle(xfer.handle);
	vkFreeCommandBuffers(ctx->device, pool, 1, &cmd);
	vkDestroyBuffer(ctx->device, staging.buf, NULL);
	vkFreeMemory(ctx->device, staging.mem, NULL);
	vkDestroyCommandPool(ctx->device, pool, NULL);
	return vimg;
}

void command_buffer_record_or_crash(VkCommandBuffer cbuf, VkRenderPass pass,
	swapchain swap, pipeline pipe, VkFramebuffer *framebuf,
	vulkan_buffer vbuf, vulkan_buffer ibuf, u32 upcoming_index)
{
	VkCommandBufferBeginInfo cmd_desc = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
	if (vkBeginCommandBuffer(cbuf, &cmd_desc) != VK_SUCCESS)
		crash("vkBeginCommandBuffer");
	VkClearValue clear[] = {
		[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}},
		[1].depthStencil = {1.0f, 0},
	};
	VkRenderPassBeginInfo pass_desc = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = pass,
		.framebuffer = framebuf[swap.current],
		.renderArea.offset = {0, 0},
		.renderArea.extent = swap.dim,
		.clearValueCount = ARRAY_SIZE(clear),
		.pClearValues = clear,
	};
	vkCmdBeginRenderPass(cbuf, &pass_desc, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(cbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.line);
	vkCmdBindVertexBuffers(cbuf, 0, 1, &vbuf.buf, &(VkDeviceSize){0});
	vkCmdBindIndexBuffer(cbuf, ibuf.buf, 0, VK_INDEX_TYPE_UINT16);
	vkCmdBindDescriptorSets(cbuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipe.layout, 0, 1, &pipe.set[upcoming_index], 0, NULL);
	u32 indx_cnt = (u32) ARRAY_SIZE(indices);
	vkCmdDrawIndexed(cbuf, indx_cnt, 1, 0, 0, 0);
	vkCmdEndRenderPass(cbuf);
	if (vkEndCommandBuffer(cbuf) != VK_SUCCESS)
		crash("vkEndCommandBuffer");
}

typedef struct {
	VkSemaphore present_ready[MAX_FRAMES_RENDERING];
	VkSemaphore render_done[MAX_FRAMES_RENDERING];
	VkFence rendering[MAX_FRAMES_RENDERING];
} fences;

void sync_create_or_crash(VkDevice logical, u32 cnt,
	VkSemaphore *present_ready, VkSemaphore *render_done, VkFence *rendering)
{
	VkSemaphoreCreateInfo sem_desc = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};
	VkFenceCreateInfo fence_desc = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};
	for (u32 i = 0; i < cnt; i++) {
		if (vkCreateSemaphore(logical, &sem_desc, NULL, &present_ready[i]) != VK_SUCCESS)
			crash("vkCreateSemaphore");
		if (vkCreateSemaphore(logical, &sem_desc, NULL, &render_done[i]) != VK_SUCCESS)
			crash("vkCreateSemaphore");
		if (vkCreateFence(logical, &fence_desc, NULL, &rendering[i]) != VK_SUCCESS)
			crash("vkCreateFence");
	}
}

void transforms_upload(void *to, float width, float height)
{
	float time = (float) glfwGetTime();
	float asp = width / height;
	transforms tfm;
	glm_rotate_make(tfm.model, time * 2.0f, (vec3){ 1.0f, 0.0f, 0.0f });
	mat4 orbital;
	glm_rotate_make(orbital, time * 0.4f, (vec3){ 0.0f, 0.0f, 1.0f });
	glm_translate(orbital, (vec3){ 1.0f, 0.0f, 0.0f });
	memcpy(tfm.model[3], orbital[3], sizeof(vec3));
	glm_lookat(
		(vec3){ 2.0f, 2.0f, 2.0f },
		(vec3){ 0.0f, 0.0f, 0.0f },
		(vec3){ 0.0f, 0.0f, 1.0f },
		tfm.view
	);
	glm_perspective((float) M_PI/4.0f, asp, 0.1f, 10.0f, tfm.proj);
	tfm.proj[1][1] *= -1.0f;
	memcpy(to, &tfm, sizeof tfm);
}

typedef struct {
	fences sync;
	VkCommandBuffer commands[MAX_FRAMES_RENDERING];
} draw_calls;

void draw_or_crash(context *ctx, draw_calls info, u32 upcoming_index,
	swapchain swap, VkFramebuffer *framebuf, VkRenderPass graphics_pass,
	pipeline pipe, vulkan_buffer vbuf, vulkan_buffer ibuf, transforms *umapped,
	vulkan_queue gqueue)
{
	// cpu wait for current frame to be done rendering
	vkWaitForFences(ctx->device, 1, &info.sync.rendering[upcoming_index], VK_TRUE, UINT64_MAX);
	vkResetFences(ctx->device, 1, &info.sync.rendering[upcoming_index]);
	vkAcquireNextImageKHR(ctx->device, swap.chain, UINT64_MAX,
		info.sync.present_ready[upcoming_index], VK_NULL_HANDLE, &swap.current);
	// recording commands for next frame
	vkResetCommandBuffer(info.commands[upcoming_index], 0);
	transforms_upload(&umapped[upcoming_index], (float) swap.dim.width, (float) swap.dim.height);
	command_buffer_record_or_crash(info.commands[upcoming_index],
		graphics_pass, swap, pipe, framebuf, vbuf, ibuf, upcoming_index);
	// submitting commands for next frame
	VkSubmitInfo submission_desc = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &info.sync.present_ready[upcoming_index],
		.pWaitDstStageMask = &(VkPipelineStageFlags){
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		},
		.commandBufferCount = 1,
		.pCommandBuffers = &info.commands[upcoming_index],
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &info.sync.render_done[upcoming_index],
	};
	if (vkQueueSubmit(gqueue.handle, 1, &submission_desc,
		info.sync.rendering[upcoming_index]) != VK_SUCCESS)
		crash("vkQueueSubmit");
	// present rendered image
	VkPresentInfoKHR present_desc = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &info.sync.render_done[upcoming_index],
		.swapchainCount = 1,
		.pSwapchains = &swap.chain,
		.pImageIndices = &swap.current,
	};
	vkQueuePresentKHR(gqueue.handle, &present_desc);
}

VkSampler sampler_create_or_crash(context *ctx)
{
	VkPhysicalDeviceProperties *props = &ctx->specs->properties;
	VkSamplerCreateInfo sm_desc = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.anisotropyEnable = VK_TRUE,
		.maxAnisotropy = props->limits.maxSamplerAnisotropy,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
		.unnormalizedCoordinates = VK_FALSE,
		.compareEnable = VK_FALSE,
		.compareOp = VK_COMPARE_OP_ALWAYS,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.minLod = 0.0f,
		.maxLod = VK_LOD_CLAMP_NONE,
	};
	VkSampler sm;
	if (vkCreateSampler(ctx->device, &sm_desc, NULL, &sm) != VK_SUCCESS)
		crash("vkCreateSampler");
	return sm;
}

typedef struct {
	vulkan_image img;
	VkImageView view;
	VkFormat fmt;
} depth_buffer;

depth_buffer depth_buffer_create(context *ctx, VkExtent2D dims)
{
	VkFormat candidates[] = {
		VK_FORMAT_D32_SFLOAT,
		VK_FORMAT_D24_UNORM_S8_UINT,
		VK_FORMAT_D32_SFLOAT_S8_UINT,
	};
	VkFormat fmt = format_supported(ctx->physical_device, ARRAY_SIZE(candidates), candidates,
		VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
	if (fmt == VK_FORMAT_UNDEFINED)
		crash("no suitable format found for a depth buffer");
	vulkan_image img = vulkan_image_create(ctx, fmt,
		dims.width, dims.height, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_TILING_OPTIMAL, 1);
	VkImageView view = image_view_create(ctx->device, img.img, fmt, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
	return (depth_buffer){ img, view, fmt };
}

static const int WIDTH = 800;
static const int HEIGHT = 600;

int main()
{
	context ctx = context_init(WIDTH, HEIGHT, "Gala");
	swapchain swap = swapchain_create(&ctx);
	depth_buffer db = depth_buffer_create(&ctx, swap.dim);
	VkRenderPass graphics_pass = render_pass_create_or_crash(ctx.device,
		swap.fmt, db.fmt);
	VkFramebuffer *framebuf = framebuf_attach_or_crash(ctx.device, swap,
		graphics_pass, db.view);
	vulkan_buffer ubuf = buffer_create_or_crash(&ctx,
		MAX_FRAMES_RENDERING * sizeof(transforms),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	vulkan_queue gqueue = vulkan_queue_ref(&ctx, ctx.specs->iq_graphics);
	vulkan_image tex_image = image_upload(&ctx,
		load_image("res/sky_bottom.png"), gqueue);
	VkImageView tex_view = image_view_create(ctx.device, tex_image.img,
		VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, tex_image.mips);
	VkSampler sampler = sampler_create_or_crash(&ctx);
	pipeline pipe = graphics_pipeline_create_or_crash("bin/shader.vert.spv", "bin/shader.frag.spv",
		ctx.device, swap.dim, graphics_pass, ubuf, tex_view, sampler);
	vulkan_buffer vbuf = data_upload(&ctx,
		sizeof vertices, vertices, gqueue,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	vulkan_buffer ibuf = data_upload(&ctx,
		sizeof indices, indices, gqueue,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
	void *umapped;
	vkMapMemory(ctx.device, ubuf.mem, 0, ubuf.size, 0, &umapped);
	VkCommandPool pool = command_pool_create_or_crash(ctx.device,
		gqueue, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	draw_calls draws;
        command_buffer_create_or_crash(ctx.device, pool, MAX_FRAMES_RENDERING, draws.commands);
	sync_create_or_crash(ctx.device, MAX_FRAMES_RENDERING,
		draws.sync.present_ready, draws.sync.render_done, draws.sync.rendering);

	u32 cpu_frame = 0;
	while (!glfwWindowShouldClose(ctx.window)) {
		double beg_time = glfwGetTime();
		glfwPollEvents();
		draw_or_crash(&ctx, draws, cpu_frame % MAX_FRAMES_RENDERING,
			swap, framebuf, graphics_pass, pipe, vbuf, ibuf, umapped,
			gqueue);
		cpu_frame++;
		double end_time = glfwGetTime();
		printf("\rframe time: %fms", (end_time - beg_time) * 1e3);
	}
	printf("\n");

	vkDeviceWaitIdle(ctx.device);
	for (u32 i = 0; i < MAX_FRAMES_RENDERING; i++) {
		vkDestroySemaphore(ctx.device, draws.sync.present_ready[i], NULL);
		vkDestroySemaphore(ctx.device, draws.sync.render_done[i], NULL);
		vkDestroyFence(ctx.device, draws.sync.rendering[i], NULL);
	}
	vkFreeCommandBuffers(ctx.device, pool, MAX_FRAMES_RENDERING, draws.commands);
	vkDestroyCommandPool(ctx.device, pool, NULL);
	vkDestroyBuffer(ctx.device, ubuf.buf, NULL);
	vkFreeMemory(ctx.device, ubuf.mem, NULL);
	vkFreeMemory(ctx.device, ibuf.mem, NULL);
	vkDestroyBuffer(ctx.device, ibuf.buf, NULL);
	vkFreeMemory(ctx.device, vbuf.mem, NULL);
	vkDestroyBuffer(ctx.device, vbuf.buf, NULL);
	vkDestroyPipeline(ctx.device, pipe.line, NULL);
	vkDestroyPipelineLayout(ctx.device, pipe.layout, NULL);
	vkDestroySampler(ctx.device, sampler, NULL);
	vkDestroyImageView(ctx.device, tex_view, NULL);
	vkDestroyImage(ctx.device, tex_image.img, NULL);
	vkFreeMemory(ctx.device, tex_image.mem, NULL);
	vkDestroyDescriptorPool(ctx.device, pipe.dpool, NULL);
	vkDestroyDescriptorSetLayout(ctx.device, pipe.set_layout, NULL);
	for (u32 i = 0; i < swap.n_slot; i++) {
		vkDestroyImageView(ctx.device, swap.view[i], NULL);
		vkDestroyFramebuffer(ctx.device, framebuf[i], NULL);
	}
	vkDestroyRenderPass(ctx.device, graphics_pass, NULL);
	vkDestroyImageView(ctx.device, db.view, NULL);
	vkDestroyImage(ctx.device, db.img.img, NULL);
	vkFreeMemory(ctx.device, db.img.mem, NULL);
	free(swap.view);
	free(swap.slot);
	vkDestroySwapchainKHR(ctx.device, swap.chain, NULL);
	context_fini(&ctx);
	return 0;
}

