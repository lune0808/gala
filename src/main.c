#define _GNU_SOURCE
#include <stdlib.h>
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
#include "swapchain.h"
#include "image.h"
#include "memory.h"
#include "hwqueue.h"
#include "lifetime.h"
#include "sync.h"

typedef struct {
	void *mem;
	size_t size;
} buffer;

buffer load_file(const char *path)
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

VkShaderModule build_shader_module(const char *path, VkDevice logical)
{
	buffer buf = load_file(path);
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

VkDescriptorSetLayout descriptor_set_lyt_create(VkDevice logical,
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

VkDescriptorPool descr_pool_create(VkDevice logical,
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

VkDescriptorSet *descr_set_create(VkDevice logical,
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

void descr_set_config(VkDevice logical, VkDescriptorSet *set,
	VkImageView tex_view, VkSampler tex_sm)
{
	VkDescriptorImageInfo tex_desc = {
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.imageView = tex_view,
		.sampler = tex_sm,
	};
	VkWriteDescriptorSet write_desc[] = {
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
		vkUpdateDescriptorSets(logical,
			ARRAY_SIZE(write_desc), write_desc, 0, NULL);
	}
}

typedef struct {
	vec3 position;
	vec3 normal;
	vec2 uv;
} vertex;

typedef struct {
	vertex *vert;
	u32 *indx;
	u32 nvert;
	u32 nindx;
} mesh;

mesh uv_sphere(u32 nx, u32 ny, float r)
{
	u32 nvert_quads = (nx + 1) * ny;
	u32 nvert = nvert_quads + 2 * nx;
	u32 nindx = 6 * nx * (ny - 1) + 2 * 3 * nx;
	char *mem = xmalloc(nvert * sizeof(vertex) + nindx * sizeof(u32));
	vertex *vert = (void*) mem;
	vertex *vcur = vert;
	for (u32 iy = 0; iy < ny; iy++) {
		float angley = (float) M_PI * (float) (iy + 1) / (float) (ny + 1);
		float rsiny = r * sinf(angley);
		float rcosy = r * cosf(angley);
		float iy_ny = (float) (iy + 1) / (float) (ny + 1);
		for (u32 ix = 0; ix <= nx; ix++) {
			float anglex = 2.0f * (float) M_PI * (float) ix / (float) nx;
			vcur->position[0] = rsiny * cosf(anglex);
			vcur->position[1] = rsiny * sinf(anglex);
			vcur->position[2] = rcosy;
			vcur->uv[0] = (float) ix / (float) nx;
			vcur->uv[1] = iy_ny;
			vcur++;
		}
	}
	for (u32 ipole = 0; ipole < nx; ipole++) {
		vert[nvert_quads + ipole].position[0] = 0.0f;
		vert[nvert_quads + ipole].position[1] = 0.0f;
		vert[nvert_quads + ipole].position[2] = r;
		vert[nvert_quads + ipole].uv[0] = ((float) ipole + 0.5f) / (float) nx;
		vert[nvert_quads + ipole].uv[1] = 0.0f;
		vert[nvert_quads + ipole + nx].position[0] = 0.0f;
		vert[nvert_quads + ipole + nx].position[1] = 0.0f;
		vert[nvert_quads + ipole + nx].position[2] = -r;
		vert[nvert_quads + ipole + nx].uv[0] = ((float) ipole + 0.5f) / (float) nx;
		vert[nvert_quads + ipole + nx].uv[1] = 1.0f;
	}
	for (vcur = vert; vcur != vert + nvert; vcur++) {
		glm_normalize_to(vcur->position, vcur->normal);
	}
	u32 *indx = (void*) (mem + nvert * sizeof(vertex));
	u32 *icur = indx;
	u32 ivert = 0;
	for (u32 irow = 0; irow < ny - 1; irow++) {
		for (u32 icol = 0; icol < nx; icol++) {
			*icur++ = ivert;
			*icur++ = ivert + nx + 1;
			*icur++ = ivert + nx + 2;
			*icur++ = ivert;
			*icur++ = ivert + nx + 2;
			*icur++ = ivert + 1;
			ivert++;
		}
		ivert++;
	}
	for (u32 ipole = 0; ipole < nx; ipole++) {
		*icur++ = nvert_quads + ipole;
		*icur++ = ipole;
		*icur++ = ipole + 1;
		*icur++ = nvert_quads + ipole + nx;
		*icur++ = nvert_quads - nx + ipole;
		*icur++ = nvert_quads - nx + ipole - 1;
	}
	return (mesh){ vert, indx, nvert, nindx };
}

void mesh_fini(mesh *m)
{
	free(m->vert);
}

VkShaderStageFlagBits shader_stage_from_name(const char *path)
{
	if (strstr(path, ".vert.spv"))
		return VK_SHADER_STAGE_VERTEX_BIT;
	if (strstr(path, ".frag.spv"))
		return VK_SHADER_STAGE_FRAGMENT_BIT;
	if (strstr(path, ".comp.spv"))
		return VK_SHADER_STAGE_COMPUTE_BIT;
	crash("unknown shader stage for file \"%s\"", path);
}

void pipeline_stage_desc(VkDevice device, u32 cnt,
	VkPipelineShaderStageCreateInfo *desc, VkShaderModule *module,
	const char **path)
{
	for (u32 i = 0; i < cnt; i++) {
		module[i] = build_shader_module(path[i], device);
		desc[i] = (VkPipelineShaderStageCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = shader_stage_from_name(path[i]),
			.module = module[i],
			.pName = "main",
		};
	}
}

void pipeline_vertex_input_desc(u32 cnt,
	VkVertexInputAttributeDescription *desc,
	VkFormat *fmt, u32 *offset)
{
	for (u32 i = 0; i < cnt; i++) {
		desc[i] = (VkVertexInputAttributeDescription){
			.binding = 0,
			.location = i,
			.format = fmt[i],
			.offset = offset[i],
		};
	}
}

typedef struct {
	VkPipeline line;
	VkPipelineLayout layout;
	VkDescriptorSetLayout set_layout;
	VkDescriptorPool dpool;
	VkDescriptorSet *set;
} pipeline;

pipeline graphics_pipeline_create(const char *vert_path, const char *frag_path,
	VkDevice logical, VkExtent2D dims, VkRenderPass gpass,
	VkImageView tex_view, VkSampler tex_sm)
{
	VkShaderModule shader_module[2];
	VkPipelineShaderStageCreateInfo stg_desc[2];
	pipeline_stage_desc(logical, 2, stg_desc, shader_module,
		(const char*[]){ vert_path, frag_path });
	VkPipelineDynamicStateCreateInfo dyn_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
	};
	VkVertexInputAttributeDescription attributes[3 + 4];
	pipeline_vertex_input_desc(3, attributes,
		(VkFormat[]){
			VK_FORMAT_R32G32B32_SFLOAT,
			VK_FORMAT_R32G32B32_SFLOAT,
			VK_FORMAT_R32G32_SFLOAT,
		},
		(u32[]){
			offsetof(vertex, position),
			offsetof(vertex, normal),
			offsetof(vertex, uv),
		});
	attributes[3].binding = 1;
	attributes[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	attributes[3].location = 3;
	attributes[3].offset = 0*sizeof(vec4);
	attributes[4].binding = 1;
	attributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	attributes[4].location = 4;
	attributes[4].offset = 1*sizeof(vec4);
	attributes[5].binding = 1;
	attributes[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	attributes[5].location = 5;
	attributes[5].offset = 2*sizeof(vec4);
	attributes[6].binding = 1;
	attributes[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	attributes[6].location = 6;
	attributes[6].offset = 3*sizeof(vec4);
	VkPipelineVertexInputStateCreateInfo vert_lyt_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 2,
		.pVertexBindingDescriptions = (VkVertexInputBindingDescription[]){
			{
				.binding = 0,
				.stride = sizeof(vertex),
				.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
			},
			{
				.binding = 1,
				.stride = sizeof(mat4),
				.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
			},
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
		.cullMode = VK_CULL_MODE_BACK_BIT,
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
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		}
	};
	VkDescriptorSetLayout set_lyt = descriptor_set_lyt_create(logical,
		ARRAY_SIZE(bind_desc), bind_desc);
	VkDescriptorPoolSize pool_sizes[] = {
		{
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = (u32) MAX_FRAMES_RENDERING,
		},
	};
	VkDescriptorPool dpool = descr_pool_create(logical,
		ARRAY_SIZE(pool_sizes), pool_sizes);
	VkDescriptorSet *set = descr_set_create(logical,
		dpool, set_lyt);
	descr_set_config(logical, set, tex_view, tex_sm);
	VkPipelineLayoutCreateInfo unif_lyt_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &set_lyt,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &(VkPushConstantRange){
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT
				    | VK_SHADER_STAGE_FRAGMENT_BIT,
			.offset = 0,
			.size = sizeof(struct push_constant_data),
		},
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

	vkDestroyShaderModule(logical, shader_module[0], NULL);
	vkDestroyShaderModule(logical, shader_module[1], NULL);
	return (pipeline){ gpipe, unif_lyt, set_lyt, dpool, set };
}

void command_buffer_begin(VkCommandBuffer cbuf, attached_swapchain *swap)
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
		.renderPass = swap->pass,
		.framebuffer = swap->framebuffer[swap->base.i_slot],
		.renderArea.offset = {0, 0},
		.renderArea.extent = swap->base.dim,
		.clearValueCount = ARRAY_SIZE(clear),
		.pClearValues = clear,
	};
	vkCmdBeginRenderPass(cbuf, &pass_desc, VK_SUBPASS_CONTENTS_INLINE);
}

void command_buffer_end(VkCommandBuffer cbuf)
{
	vkCmdEndRenderPass(cbuf);
	if (vkEndCommandBuffer(cbuf) != VK_SUCCESS)
		crash("vkEndCommandBuffer");
}

typedef struct {
	vec3 offset;      // initial position & phase
	vec3 axis;        // orbiting axis
	float speed;      // running orbit speed
	vec3 self_axis;   // self rotation axis
	float self_speed; // self rotation speed
	u32 parent;
} orbiting;

typedef struct {
	u32 height;
	u32 n_orbit;
	vec4 *worldpos;
	orbiting *orbit_specs;
	u32 *index;
	mat4 *tfm;
} orbit_tree;

void rand_init()
{
	srand(0x7819e801u);
}

u32 rand_u32(u32 min, u32 max)
{
	if (min == max)
		return min;
	return min + (u32) rand() % (max - min);
}

float rand_float(float min, float max)
{
	return min + (max - min) * ((float) rand() / (float) RAND_MAX);
}

float rand_bell_like_01()
{
	float unif = rand_float(0.0f, 1.0f);
	if (unif < 0.5f) {
		return sqrtf(unif * 0.5f);
	} else {
		return 1.0f - sqrtf(1.0f - unif * 0.5f);
	}
}

void rand_vec3_dir(float zmin, float zmax, vec3 dest)
{
	float xy_angle = rand_float(0.0f, 2.0f * (float) M_PI);
	float z_angle = rand_float(zmin, zmax);
	float sinz = sinf(z_angle);
	dest[0] = sinz * cosf(xy_angle);
	dest[1] = sinz * sinf(xy_angle);
	dest[2] = cosf(z_angle);
}

void rand_vec3_shell(float zmin, float zmax, float rmin, float rmax, vec3 dest)
{
	rand_vec3_dir(zmin, zmax, dest);
	float r = rmin + (rmax - rmin) * rand_bell_like_01();
	glm_vec3_scale(dest, r, dest);
}

orbit_tree orbit_tree_init(u32 cnt)
{
	u32 n_orbit = 1 + cnt;
	char *mem = xmalloc(n_orbit * (sizeof(vec4) + sizeof(orbiting) + sizeof(u32)));
	vec4 *worldpos = (void*) mem;
	orbiting *orbit_specs = (void*) (mem + n_orbit * sizeof(vec4));
	u32 *parent = (void*) (mem + n_orbit * (sizeof(orbiting) + sizeof(vec4)));
	orbit_specs[0] = (orbiting){ {}, {0.0f, 0.0f, 1.0f}, 0.0f, {1.0f, 0.0f, 0.0f}, 0.0f, 0 };
	worldpos[0][3] = 0.0f;
	orbit_specs[1] = (orbiting){ {}, {0.0f, 0.0f, 1.0f}, 0.0f, {0.0f, 0.0f, 1.0f}, 1.0f, 0 };
	worldpos[1][3] = 1.0f;
	const float PI = (float) M_PI;
	for (u32 i = 2; i < cnt; i++) {
		orbiting *o = &orbit_specs[i];
		rand_vec3_shell(0.485f * PI, 0.515f * PI, 2.0f, 40.0f, o->offset);
		worldpos[i][3] = rand_float(1.0f/64.0f, 1.0f/8.0f);
		rand_vec3_dir(0.0f, 1.0f/96.0f * PI, o->axis);
		o->speed = rand_float(0.5f, 0.65f);
		rand_vec3_dir(0.0f, 0.25f * PI, o->self_axis);
		o->self_speed = rand_float(-4.0f, +4.0f);
		o->parent = 1;
	}
	for (u32 i = 0; i < n_orbit; i++) {
		parent[i] = i;
	}
	return (orbit_tree){ 2, n_orbit, worldpos, orbit_specs, parent, xmalloc(n_orbit * sizeof(mat4)) };
}

void orbit_tree_fini(orbit_tree *tree)
{
	free(tree->worldpos);
	free(tree->tfm);
}

void flatten_once(orbit_tree *tree, float time)
{
	// skip first element, it does nothing
	// iterate in reverse so it's like the buffers are immutable
	for (u32 i = tree->n_orbit - 1; i > 0; i--) {
		u32 index = tree->index[i];
		orbiting *orbit = &tree->orbit_specs[index];
		vec3 offset;
		memcpy(offset, orbit->offset, sizeof offset);
		glm_vec3_rotate(offset, orbit->speed * time, orbit->axis);
		glm_vec3_add(offset, tree->worldpos[i], tree->worldpos[i]);
		tree->index[i] = orbit->parent;
	}
}

void orbit_tree_index(orbit_tree *tree, u32 i, float time, mat4 dest)
{
	orbiting *orbit = &tree->orbit_specs[i];
	glm_rotate_make(dest, orbit->self_speed * time, orbit->self_axis);
	float scale = tree->worldpos[i][3];
	glm_scale(dest, (vec3){ scale, scale, scale });
	memcpy(dest[3], tree->worldpos[i], sizeof(vec3));
	dest[3][3] = (float) (i & 1);
}

typedef struct {
	mat4 tfm;
	float flat_angle;
	float azim_angle;
	vec3 pos;
	float aspect; // w/h
	float fov_rad;
	float near;
	float far;
} camera;

bool in_interval(float test, float min, float max)
{
	return min <= test && test <= max;
}

void perspective_divide(vec4 h, vec3 v)
{
	v[0] = h[0] / h[3];
	v[1] = h[1] / h[3];
	v[2] = h[2] / h[3];
}

bool in_clip(vec3 clip)
{
	// allow for a bit of extra stuff to be rendered
	// so that if the camera turns fast it is still
	// shown
	const float edge = 1.1f;
	return in_interval(clip[0], -edge, +edge)
	    && in_interval(clip[1], -edge, +edge)
	    && in_interval(clip[2],  0.0f,  edge);
}

bool visible(float scl, mat4 model, camera *cam)
{
	mat4 mvp;
	memcpy(mvp, model, sizeof(mat4));
	mvp[3][3] = 1.0f;
	glm_mat4_mul(cam->tfm, mvp, mvp);
	vec4 lbound = { -scl * 0.5f, -scl * 0.5f, -scl * 0.5f, 1.0f };
	vec4 ubound = { +scl * 0.5f, +scl * 0.5f, +scl * 0.5f, 1.0f };
	glm_mat4_mulv(mvp, lbound, lbound);
	glm_mat4_mulv(mvp, ubound, ubound);
	vec3 lclip;
	vec3 uclip;
	perspective_divide(lbound, lclip);
	perspective_divide(ubound, uclip);
	// this should also hide planets
	// that are much bigger than the screen
	return in_clip(lclip) || in_clip(uclip);
}

struct orbit_tree_sorting_data {
	vec4 *worldpos;
	vec3 cam_pos;
};

int orbit_tree_node_cmp(const void *l_, const void *r_, void *data_)
{
	struct orbit_tree_sorting_data *data = data_;
	const u32 l = *(u32*) l_;
	const u32 r = *(u32*) r_;
	float dl2 = glm_vec3_distance2(data->cam_pos, data->worldpos[l]);
	float dr2 = glm_vec3_distance2(data->cam_pos, data->worldpos[r]);
	float sl = data->worldpos[l][3];
	float sr = data->worldpos[r][3];
	float diff = dl2 / (sl * sl) - dr2 / (sr * sr);
	if (diff < 0.0f) {
		return -1;
	} else if (diff == 0.0f) {
		return 0;
	} else {
		return +1;
	}
}

u32 flatten(orbit_tree *tree, float time, camera *cam)
{
	// fast
	for (u32 i = 0; i < tree->n_orbit; i++) {
		memset(tree->worldpos[i], 0, sizeof(vec3));
		tree->index[i] = i;
	}
	// not fast
	for (u32 i = 0; i < tree->height; i++) {
		flatten_once(tree, time);
	}
	// fast
	for (u32 i = 0; i < tree->n_orbit - 1; i++) {
		tree->index[i] = i + 1;
	}
	// slow
	struct orbit_tree_sorting_data data;
	data.worldpos = tree->worldpos;
	memcpy(data.cam_pos, cam->pos, sizeof(vec3));
	qsort_r(tree->index, tree->n_orbit - 1, sizeof(u32), orbit_tree_node_cmp, &data);
	u32 n_visible = 0;
	// slow (same time)
	for (u32 i = 0; i < tree->n_orbit - 1; i++) {
		u32 sorted = tree->index[i];
		assert(sorted < tree->n_orbit && sorted != 0);
		orbit_tree_index(tree, sorted, time, tree->tfm[n_visible]);
		if (visible(tree->worldpos[sorted][3], tree->tfm[n_visible], cam)) {
			n_visible++;
		}
	}
	return n_visible;
}

void camera_matrix(camera *cam)
{
	vec3 in_front = {
		cosf(cam->azim_angle) * sinf(cam->flat_angle),
		cosf(cam->azim_angle) * cosf(cam->flat_angle),
		sinf(cam->azim_angle)
	};
	glm_vec3_add(cam->pos, in_front, in_front);
	mat4 view;
	glm_lookat(cam->pos, in_front, (vec3){ 0.0f, 0.0f, 1.0f }, view);
	mat4 proj;
	glm_perspective(cam->fov_rad, cam->aspect, cam->near, cam->far, proj);
	proj[1][1] *= -1.0f;
	glm_mat4_mul(proj, view, cam->tfm);
}

void camera_axes(camera *cam, mat3 axes)
{
	axes[2][0] = axes[2][1] = axes[1][2] = 0.0f;
	axes[2][2] = 1.0f;
	axes[1][0] = sinf(cam->flat_angle);
	axes[1][1] = cosf(cam->flat_angle);
	glm_vec3_cross(axes[1], axes[2], axes[0]);
}

camera camera_init(VkExtent2D range, vec3 pos, vec3 target, GLFWwindow *window)
{
	camera cam;
	cam.fov_rad = (float) M_PI / 4.0f;
	cam.aspect = (float) range.width / (float) range.height;
	cam.far = 100.0f;
	cam.near = 0.1f;
	memcpy(cam.pos, pos, sizeof(vec3));
	vec3 dir;
	glm_vec3_sub(target, pos, dir);
	cam.flat_angle = atan2f(-dir[0], dir[1]);
	cam.azim_angle = atan2f(dir[2], glm_vec2_norm(dir));

	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	if (!glfwRawMouseMotionSupported())
		crash("raw mouse motion not available on the platform");
	glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
	return cam;
}

void camera_update(camera *cam, context *ctx, float dt)
{
	float speed = 2.0f;
	mat3 axes;
	camera_axes(cam, axes);
	GLFWwindow *window = ctx->window;
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		glm_vec3_muladds(axes[0], -dt * speed, cam->pos);
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		glm_vec3_muladds(axes[0], +dt * speed, cam->pos);
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		glm_vec3_muladds(axes[1], -dt * speed, cam->pos);
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		glm_vec3_muladds(axes[1], +dt * speed, cam->pos);
	if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
		glm_vec3_muladds(axes[2], +dt * speed, cam->pos);
	if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
		glm_vec3_muladds(axes[2], -dt * speed, cam->pos);
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GLFW_TRUE);
	float rot_speed = 1.0f;
	// dx/1 = tan(ax) = ax << 1
	float dx = +rot_speed * ctx->mouse.dx;
	float dy = -rot_speed * ctx->mouse.dy;
	cam->flat_angle = fmodf(cam->flat_angle + dx, (float) M_PI * 2.0f);
	cam->azim_angle = CLAMP(
		cam->azim_angle + dy,
		(float) M_PI * -0.45f,
		(float) M_PI * +0.45f
	);
}

void push_constant_populate(struct push_constant_data *pushc, camera *cam)
{
	memcpy(pushc->viewproj, cam->tfm, sizeof(mat4));
}

typedef struct {
	vulkan_buffer vert;
	vulkan_buffer indx;
} uploaded_mesh;

uploaded_mesh mesh_upload(context *ctx, mesh m,
	lifetime *cpuside, lifetime *gpuside)
{
	vulkan_buffer vert = data_upload(ctx,
		m.nvert * sizeof(vertex), m.vert,
		cpuside, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	lifetime_bind_buffer(gpuside, vert);
	vulkan_buffer indx = data_upload(ctx,
		m.nindx * sizeof(u32), m.indx,
		cpuside, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
	lifetime_bind_buffer(gpuside, indx);
	return (uploaded_mesh){ vert, indx };
}

void draw(context *ctx, attached_swapchain *sc, pipeline *pipe,
	uploaded_mesh *mesh, orbit_tree *tree, camera *cam,
	vulkan_buffer instbuf, char *mapped)
{
	// render
	float now = (float) glfwGetTime();
	u32 ilod[5];
	u32 n_visible = flatten(tree, now, cam);
	ilod[0] = 0;
	for (u32 i = 1; i < ARRAY_SIZE(ilod); i++) {
		ilod[i] = n_visible;
	}
	u32 iilod = 1;
	float dist2_max[ARRAY_SIZE(ilod)-2] = { 5e1f, 5e3f, 5e4f };
	for (u32 i = 0; i < n_visible; i++) {
		u32 sorted = tree->index[i];
		float d2 = glm_vec3_distance2(cam->pos, tree->worldpos[sorted]);
		float s = tree->worldpos[sorted][3];
		assert(s > 0.0f);
		if (d2 / (s * s) > dist2_max[iilod-1]) {
			ilod[iilod++] = i;
			if (iilod == ARRAY_SIZE(ilod)-1)
				break;
		}
	}
	float render_time = (float) glfwGetTime() - now;
	printf("    cpu render:%.2fms   l0:%u   l1:%u   l2:%u   l3:%u   l4:%u   ",
		render_time * 1e3f, ilod[0], ilod[1], ilod[2], ilod[3], ilod[4]);

	VkDeviceSize inst_size = tree->n_orbit * sizeof(mat4);
	VkDeviceSize inst_index = (sc->frame_indx + 1) % MAX_FRAMES_RENDERING;
	VkDeviceSize next_index = (sc->frame_indx + 2) % MAX_FRAMES_RENDERING;
	memcpy(mapped + next_index * inst_size, tree->tfm, n_visible * sizeof(mat4));
	// cpu wait for current frame to be done rendering
	attached_swapchain_swap_buffers(ctx, sc);
	// recording commands for next frame
	VkCommandBuffer cmd = attached_swapchain_current_graphics_cmd(sc);
	vkResetCommandBuffer(cmd, 0);
	command_buffer_begin(cmd, sc);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe->line);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipe->layout, 0, 1, &pipe->set[sc->frame_indx], 0, NULL);
	struct push_constant_data pushc;
	push_constant_populate(&pushc, cam);
	VkDeviceSize offsets[] = { 0, inst_index * inst_size };
	for (u32 iilod = 0; iilod < ARRAY_SIZE(ilod)-1; iilod++) {
		if (ilod[iilod] == ilod[iilod+1])
			continue;
		vkCmdBindIndexBuffer(cmd, mesh[iilod].indx.handle, 0, VK_INDEX_TYPE_UINT32);
		VkBuffer buffers[] = { mesh[iilod].vert.handle, instbuf.handle };
		vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
		pushc.lod = (float) iilod;
		vkCmdPushConstants(cmd, pipe->layout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(pushc), &pushc);
		vkCmdDrawIndexed(cmd, (u32) mesh[iilod].indx.size / sizeof(u32), ilod[iilod+1] - ilod[iilod], 0, 0, ilod[iilod]);
	}
	command_buffer_end(cmd);

	// submitting commands for next frame
	VkSubmitInfo submission_desc = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = attached_swapchain_current_present_ready(sc),
		.pWaitDstStageMask = &(VkPipelineStageFlags){
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		},
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = attached_swapchain_current_render_done(sc),
	};
	if (vkQueueSubmit(sc->graphics_queue.handle, 1, &submission_desc,
		attached_swapchain_current_rendering(sc)) != VK_SUCCESS)
		crash("vkQueueSubmit");
	// present rendered image
	attached_swapchain_present(sc);
}

VkSampler sampler_create(context *ctx)
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

static const int WIDTH = 1600;
static const int HEIGHT = 900;

int main()
{
	context ctx = context_init(WIDTH, HEIGHT, "Gala");
	attached_swapchain sc = attached_swapchain_create(&ctx);
	lifetime window_lifetime = lifetime_init(&ctx, sc.graphics_queue, 0, 0);
	lifetime loading_lifetime = lifetime_init(&ctx, sc.graphics_queue,
		VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
		| VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, 4);
	vulkan_bound_image textures = vulkan_bound_image_upload(&ctx,
		2, (loaded_image[]){
			load_image("res/2k_venus_surface.jpg"),
			load_image("res/2k_mercury.jpg"),
		}, &loading_lifetime);
	lifetime_bind_image(&window_lifetime, textures);
	VkSampler sampler = sampler_create(&ctx);
	lifetime_bind_sampler(&window_lifetime, sampler);
	pipeline pipe = graphics_pipeline_create("bin/shader.vert.spv", "bin/shader.frag.spv",
		ctx.device, sc.base.dim, sc.pass, textures.view, sampler);
	uploaded_mesh lod[4];
	mesh m = uv_sphere(64, 48, 0.5f);
	lod[0] = mesh_upload(&ctx, m,
		&loading_lifetime, &window_lifetime);
	mesh_fini(&m);
	m = uv_sphere(16, 12, 0.5f);
	lod[1] = mesh_upload(&ctx, m,
		&loading_lifetime, &window_lifetime);
	mesh_fini(&m);
	m = uv_sphere(6, 3, 0.5f);
	lod[2] = mesh_upload(&ctx, m,
		&loading_lifetime, &window_lifetime);
	mesh_fini(&m);
	m = uv_sphere(3, 2, 0.5f);
	lod[3] = mesh_upload(&ctx, m,
		&loading_lifetime, &window_lifetime);
	mesh_fini(&m);
	orbit_tree tree = orbit_tree_init(1u << 16);
	camera cam = camera_init(sc.base.dim,
		(vec3){ 0.0f, -12.0f, 2.0f },
		(vec3){ 0.0f, 0.0f, 0.0f },
		ctx.window
	);
	float dt = 0.0f;
	context_ignore_mouse_once(&ctx);
	lifetime_fini(&loading_lifetime, &ctx);
	vulkan_buffer instbuf = buffer_create(&ctx,
		MAX_FRAMES_RENDERING * tree.n_orbit * sizeof(mat4),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	void *mapped = buffer_map(&ctx, instbuf);

	while (context_keep(&ctx)) {
		double beg_time = glfwGetTime();
		camera_update(&cam, &ctx, dt);
		camera_matrix(&cam);
		draw(&ctx, &sc, &pipe, lod, &tree, &cam, instbuf, mapped);
		double end_time = glfwGetTime();
		printf("\rframe time: %.2fms", (end_time - beg_time) * 1e3);
		dt = (float) (end_time - beg_time);
	}
	printf("\n");
	orbit_tree_fini(&tree);

	vkDeviceWaitIdle(ctx.device);
	vkDestroyBuffer(ctx.device, instbuf.handle, NULL);
	vkFreeMemory(ctx.device, instbuf.mem, NULL);
	vkDestroyPipeline(ctx.device, pipe.line, NULL);
	vkDestroyPipelineLayout(ctx.device, pipe.layout, NULL);
	vkDestroyDescriptorPool(ctx.device, pipe.dpool, NULL);
	vkDestroyDescriptorSetLayout(ctx.device, pipe.set_layout, NULL);
	lifetime_fini(&window_lifetime, &ctx);
	attached_swapchain_destroy(&ctx, &sc);
	context_fini(&ctx);
	return 0;
}

