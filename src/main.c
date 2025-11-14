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
#include <cglm/clipspace/persp_rh_zo.h>
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

void descr_set_create(VkDevice logical, u32 n_set, VkDescriptorSet *set,
	VkDescriptorPool pool, VkDescriptorSetLayout *lyt)
{
	VkDescriptorSetAllocateInfo alloc_desc = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = pool,
		.descriptorSetCount = n_set,
		.pSetLayouts = lyt,
	};
	if (vkAllocateDescriptorSets(logical, &alloc_desc, set) != VK_SUCCESS)
		crash("vkAllocateDescriptorSets");
}

VkWriteDescriptorSet unbound_descriptor_config(u32 binding, VkDescriptorType type)
{
	return (VkWriteDescriptorSet){
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstBinding = binding,
		.dstArrayElement = 0,
		.descriptorType = type,
		.descriptorCount = 1,
	};
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

u32 uv_sphere_vert_size(u32 nx, u32 ny)
{
	u32 nvert_quads = (nx + 1) * ny;
	u32 nvert = nvert_quads + 2 * nx;
	return nvert * sizeof(vertex);
}

u32 uv_sphere_indx_size(u32 nx, u32 ny)
{
	u32 nindx = 6 * nx * (ny - 1) + 2 * 3 * nx;
	return nindx * sizeof(u32);
}

mesh uv_sphere(u32 nx, u32 ny, float r, vertex *vert, u32 *indx)
{
	u32 nvert_quads = (nx + 1) * ny;
	u32 nvert = nvert_quads + 2 * nx;
	u32 nindx = 6 * nx * (ny - 1) + 2 * 3 * nx;
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

enum { MAX_DESCRIPTOR_SETS = 8 };

typedef struct {
	VkPipelineLayout handle;
	VkDescriptorSetLayout descset;
	VkDescriptorPool pool;
	VkDescriptorSet set[MAX_DESCRIPTOR_SETS];
} pipeline_layout;

VkDescriptorSetLayoutBinding descset_layout_binding(u32 binding,
	VkDescriptorType type, VkShaderStageFlags access)
{
	return (VkDescriptorSetLayoutBinding){
		.binding = binding,
		.descriptorType = type,
		.descriptorCount = 1,
		.stageFlags = access,
	};
}

// description points to an array of union { VkDescriptorBufferInfo; VkDescriptorImageInfo }*
pipeline_layout pipeline_layout_create(VkDevice device, u32 n_set,
	u32 n_bind, VkDescriptorSetLayoutBinding *bind, void **description,
	u32 n_poolz, VkDescriptorPoolSize *poolz,
	VkPushConstantRange *pushconstant)
{
	VkDescriptorSetLayout set_layout[MAX_DESCRIPTOR_SETS];
	set_layout[0] = descriptor_set_lyt_create(device, n_bind, bind);
	for (u32 i = 1; i < n_set; i++) {
		set_layout[i] = set_layout[0];
	}
	VkDescriptorPool pool = descr_pool_create(device,
		n_poolz, poolz);
	VkDescriptorSet set[MAX_DESCRIPTOR_SETS];
	descr_set_create(device, n_set, set, pool, set_layout);
	VkWriteDescriptorSet write[n_bind];
	for (u32 iset = 0; iset < n_set; iset++) {
		for (u32 ibind = 0; ibind < n_bind; ibind++) {
			VkDescriptorType type = bind[ibind].descriptorType;
			write[ibind] = unbound_descriptor_config(bind[ibind].binding,
				type);
			write[ibind].dstSet = set[iset];
			switch (type) {
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
				write[ibind].pBufferInfo = description[ibind];
				break;
			case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
				write[ibind].pImageInfo = description[ibind];
				break;
			default:
				crash("unhandled descriptor type %x", type);
			}
		}
		vkUpdateDescriptorSets(
			device,
			n_bind, write,
			0, NULL
		);
	}
	VkPipelineLayoutCreateInfo layout_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &set_layout[0],
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = pushconstant,
	};
	VkPipelineLayout layout;
	if (vkCreatePipelineLayout(device, &layout_desc, NULL, &layout) != VK_SUCCESS)
		crash("vkCreatePipelineLayout");
	pipeline_layout result;
	result.handle = layout;
	result.descset = set_layout[0];
	result.pool = pool;
	memcpy(result.set, set, n_set * sizeof(*set));
	return result;
}

void pipeline_layout_destroy(VkDevice device, pipeline_layout *layout)
{
	vkDestroyPipelineLayout(device, layout->handle, NULL);
	vkDestroyDescriptorPool(device, layout->pool, NULL);
	vkDestroyDescriptorSetLayout(device, layout->descset, NULL);
}

void pipeline_stage_desc(VkDevice device,
	VkPipelineShaderStageCreateInfo *desc, VkShaderModule *module,
	const char *path)
{
	*module = build_shader_module(path, device);
	desc->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	desc->pNext = NULL;
	desc->flags = 0;
	desc->stage = shader_stage_from_name(path);
	desc->module = *module;
	desc->pName = "main";
	desc->pSpecializationInfo = NULL;
}

void pipeline_vertex_input_desc(VkVertexInputAttributeDescription *desc,
	u32 location, VkFormat fmt, u32 offset)
{
	desc->binding = 0;
	desc->location = location;
	desc->format = fmt;
	desc->offset = offset;
}

VkPipeline graphics_pipeline_create(const char *vert_path, const char *frag_path,
	VkDevice logical, VkExtent2D dims, VkRenderPass gpass, u32 subpass,
	pipeline_layout *layout)
{
	VkShaderModule shader_module[2];
	VkPipelineShaderStageCreateInfo stg_desc[2];
	pipeline_stage_desc(logical, &stg_desc[0], &shader_module[0], vert_path);
	pipeline_stage_desc(logical, &stg_desc[1], &shader_module[1], frag_path);
	VkPipelineDynamicStateCreateInfo dyn_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
	};
	VkVertexInputAttributeDescription attributes[3];
	pipeline_vertex_input_desc(&attributes[0], 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex, position));
	pipeline_vertex_input_desc(&attributes[1], 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex, normal  ));
	pipeline_vertex_input_desc(&attributes[2], 2, VK_FORMAT_R32G32_SFLOAT   , offsetof(vertex, uv      ));
	VkPipelineVertexInputStateCreateInfo vert_lyt_desc = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = (VkVertexInputBindingDescription[]){
			{
				.binding = 0,
				.stride = sizeof(vertex),
				.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
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
		.depthCompareOp = VK_COMPARE_OP_GREATER,
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
		.layout = layout->handle,
		.renderPass = gpass,
		.subpass = subpass,
	};
	VkPipeline gpipe;
	if (vkCreateGraphicsPipelines(logical, VK_NULL_HANDLE, 1, &gpipe_desc, NULL, &gpipe) != VK_SUCCESS)
		crash("vkCreateGraphicsPipelines");

	vkDestroyShaderModule(logical, shader_module[0], NULL);
	vkDestroyShaderModule(logical, shader_module[1], NULL);
	return gpipe;
}

VkPipeline compute_pipeline_create(const char *comp_path, VkDevice device,
	pipeline_layout *layout)
{
	VkShaderModule module;
	VkPipelineShaderStageCreateInfo stg_desc;
	pipeline_stage_desc(device, &stg_desc, &module, comp_path);
	VkComputePipelineCreateInfo pipe_desc = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = stg_desc,
		.layout = layout->handle,
	};
	VkPipeline pipe;
	if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipe_desc, NULL,
		&pipe) != VK_SUCCESS)
		crash("vkCreateComputePipelines");
	vkDestroyShaderModule(device, module, NULL);
	return pipe;
}

void command_buffer_begin(VkCommandBuffer cbuf)
{
	VkCommandBufferBeginInfo cmd_desc = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
	if (vkBeginCommandBuffer(cbuf, &cmd_desc) != VK_SUCCESS)
		crash("vkBeginCommandBuffer");
}

void command_buffer_end(VkCommandBuffer cbuf)
{
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

enum {
	OT_TFM = 0,
	OT_SELFROT = sizeof(mat4),
	OT_WORLDPOS = OT_SELFROT + sizeof(versor[2]),
	OT_ORBIT_SPECS = OT_WORLDPOS + sizeof(vec4),
	OT_INDEX = OT_ORBIT_SPECS + sizeof(orbiting),
	OT_SORTKEY = OT_INDEX + sizeof(u32),
	OT_TEX = OT_SORTKEY + sizeof(float),
	OT_ALL = OT_TEX + sizeof(u32),
};

typedef struct {
	u32 height;
	u32 n_orbit;

	mat4 *tfm;
	versor (*selfrot)[2]; // orientation, velocity
	vec4 *worldpos;
	orbiting *orbit_specs;
	u32 *index;
	float *sortkey;
	u32 *tex;

	struct orbit_spec *uploading_orbit_specs;
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
	return sqrtf(unif);
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

float rand_vec3_shell(float zmin, float zmax, float rmin, float rmax, vec3 dest)
{
	rand_vec3_dir(zmin, zmax, dest);
	float r = rmin + (rmax - rmin) * rand_bell_like_01();
	glm_vec3_scale(dest, r, dest);
	return r;
}

orbit_tree orbit_tree_init(u32 cnt)
{
	u32 n_orbit = 1 + cnt;
	char *mem = xmalloc(n_orbit * OT_ALL);
	mat4 *tfm = (void*) mem;
	versor (*selfrot)[2] = (void*) (mem + n_orbit * OT_SELFROT);
	vec4 *worldpos = (void*) (mem + n_orbit * OT_WORLDPOS);
	orbiting *orbit_specs = (void*) (mem + n_orbit * OT_ORBIT_SPECS);
	u32 *index = (void*) (mem + n_orbit * OT_INDEX);
	float *sortkey = (void*) (mem + n_orbit * OT_SORTKEY);
	u32 *tex = (void*) (mem + n_orbit * OT_TEX);

	orbit_specs[0] = (orbiting){ {}, {0.0f, 0.0f, 1.0f}, 0.0f, {1.0f, 0.0f, 0.0f}, 0.0f, 0 };
	worldpos[0][3] = 0.0f;
	orbit_specs[1] = (orbiting){ {}, {0.0f, 0.0f, 1.0f}, 0.0f, {0.0f, 0.0f, 1.0f}, 1.0f, 0 };
	worldpos[1][3] = 1.0f;
	tex[0] = 0;
	tex[1] = 0;
	const float PI = (float) M_PI;
	for (u32 i = 2; i < cnt; i++) {
		orbiting *o = &orbit_specs[i];
		float r = rand_vec3_shell(0.485f * PI, 0.515f * PI, 2.0f, 64.0f, o->offset);
		worldpos[i][3] = rand_float(1.0f/64.0f, 1.0f/8.0f) * 1.4f;
		rand_vec3_dir(0.0f, r / 1200.0f * PI, o->axis);
		o->speed = rand_float(0.5f, 0.65f) / (r * r) * 30.0f;
		rand_vec3_dir(0.0f, 0.25f * PI, o->self_axis);
		o->self_speed = rand_float(-4.0f, +4.0f);
		o->parent = 1;
		tex[i] = rand_u32(1, 12);
	}
	for (u32 i = 0; i < n_orbit; i++) {
		orbiting *o = &orbit_specs[i];
		glm_quat_identity(selfrot[i][0]);
		// q = exp(u.theta/2)
		// q' = u.theta'/2.q
		// q(t+dt) = (1+dt.u.theta'/2).q(t)
		glm_vec3_scale(o->self_axis, 0.5f * o->self_speed, selfrot[i][1]);
		selfrot[i][1][3] = 0.0f;
		index[i] = i;
	}

	struct orbit_spec *upload = xmalloc(sizeof(*upload));
	for (u32 i = 0; i < n_orbit; i++) {
		memcpy(upload->startoffset[i], orbit_specs[i].offset, sizeof(vec3));
		versor orient;
		vec3 omega;
		glm_quat_identity(orient);
		glm_vec3_scale(orbit_specs[i].axis, 0.5f * orbit_specs[i].speed, omega);
		memcpy(upload->orbitorient[i], orient, sizeof(versor));
		memcpy(upload->orbitderiv[i], omega, sizeof(vec3));
		upload->itemscale[i] = worldpos[i][3];
		upload->texindex[i] = (float) tex[i];
		upload->parent[i] = orbit_specs[i].parent;
		memcpy(upload->selforient[i], selfrot[i][0], sizeof(versor));
		memcpy(upload->selfderiv[i], selfrot[i][1], sizeof(versor));
	}

	return (orbit_tree){ 2, n_orbit, tfm, selfrot,
		worldpos, orbit_specs, index, sortkey, tex, upload };
}

void orbit_tree_fini(orbit_tree *tree)
{
	free(tree->tfm);
	free(tree->uploading_orbit_specs);
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

void orbit_tree_index(orbit_tree *tree, u32 i, float dt, mat4 dest)
{
	versor dq;
	glm_vec4_scale(tree->selfrot[i][1], dt, dq);
	dq[3] = 1.0f;
	glm_quat_mul(dq, tree->selfrot[i][0], tree->selfrot[i][0]);
	glm_quat_normalize(tree->selfrot[i][0]);
	glm_quat_mat4(tree->selfrot[i][0], dest);
	float scale = tree->worldpos[i][3];
	glm_scale(dest, (vec3){ scale, scale, scale });
	memcpy(dest[3], tree->worldpos[i], sizeof(vec3));
	dest[3][3] = (float) tree->tex[i];
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
	vec3 tmp;
	memcpy(tmp, lclip, sizeof(vec3));
	glm_vec3_minv(tmp, uclip, lclip);
	glm_vec3_maxv(tmp, uclip, uclip);
	// this should also hide planets
	// that are much bigger than the screen
	return in_clip(lclip) || in_clip(uclip);
}

int orbit_tree_node_cmp(const void *l_, const void *r_, void *data_)
{
	float *sortkeys = data_;
	const u32 l = *(u32*) l_;
	const u32 r = *(u32*) r_;
	if (sortkeys[l] - sortkeys[r] < 0.0f) {
		return -1;
	} else {
		return +1;
	}
}

__attribute__((unused))
static float time_now_ms()
{
	return (float) glfwGetTime() * 1e3f;
}

u32 flatten(orbit_tree *tree, float time, float dt, camera *cam, u32 *ilod)
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
		float scale = tree->worldpos[i + 1][3];
		tree->sortkey[i + 1] = glm_vec3_distance2(cam->pos, tree->worldpos[i + 1]) / (scale * scale);
		tree->index[i] = i + 1;
	}
	// slow
	qsort_r(tree->index, tree->n_orbit - 1, sizeof(u32), orbit_tree_node_cmp, tree->sortkey);
	u32 iilod = 0;
	u32 n_visible = 0;
	// slower
	float dist2_max[5] = { 0.0f, 5e1f, 5e3f, 5e4f, INFINITY };
	for (u32 i = 0; i < tree->n_orbit - 1; i++) {
		u32 sorted = tree->index[i];
		assert(sorted < tree->n_orbit && sorted != 0);
		mat4 pos;
		glm_translate_make(pos, tree->worldpos[sorted]);
		float scale = tree->worldpos[sorted][3];
		glm_scale(pos, (vec3){ scale, scale, scale });
		if (visible(scale, pos, cam)) {
			if (tree->sortkey[sorted] > dist2_max[iilod]) {
				ilod[iilod++] = n_visible;
			}
			if (iilod < 3) {
				orbit_tree_index(tree, sorted, dt, tree->tfm[n_visible]);
			} else {
				memcpy(tree->tfm[n_visible], pos, sizeof(mat4));
				tree->tfm[n_visible][3][3] = (float) tree->tex[sorted];
			}
			n_visible++;
		}
	}
	ilod[iilod] = n_visible;
	return iilod;
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
	glm_perspective_rh_zo(cam->fov_rad, cam->aspect, cam->far, cam->near, proj);
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
	cam.far = 10000.0f;
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

void push_constant_populate(struct push_constant_data *pushc, camera *cam,
	u32 index, float time, float dt, u32 tree_height, u32 tree_n)
{
	memcpy(pushc->viewproj, cam->tfm, sizeof(mat4));
	memcpy(pushc->cam_pos, cam->pos, sizeof(vec3));
	pushc->baseindex = index;
	pushc->time = time;
	pushc->dt = dt;
	pushc->tree_height = tree_height;
	pushc->tree_n = tree_n;
}

typedef struct {
	vulkan_buffer vert;
	vulkan_buffer indx;
	u32 *vbase;
	u32 *ibase;
	u32 n_mesh;
} uploaded_mesh;

uploaded_mesh mesh_upload(context *ctx, u32 n_mesh, mesh *m,
	lifetime *cpuside, lifetime *gpuside)
{
	char *mem = xmalloc((n_mesh + 1) * sizeof(u32) * 2);
	u32 *vbase = (void*) mem;
	u32 *ibase = (void*) (mem + (n_mesh + 1) * sizeof(u32));
	vbase[0] = 0;
	ibase[0] = 0;
	for (u32 i = 0; i < n_mesh; i++) {
		vbase[i + 1] = vbase[i] + m[i].nvert;
		ibase[i + 1] = ibase[i] + m[i].nindx;
	}
	vulkan_buffer vert = data_upload(ctx,
		vbase[n_mesh] * sizeof(vertex), m[0].vert,
		cpuside, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	lifetime_bind_buffer(gpuside, vert);
	vulkan_buffer indx = data_upload(ctx,
		ibase[n_mesh] * sizeof(u32), m[0].indx,
		cpuside, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
	lifetime_bind_buffer(gpuside, indx);
	return (uploaded_mesh){ vert, indx, vbase, ibase, n_mesh };
}

// TODO: add element size to buffer and index to this function
VkBufferMemoryBarrier barrier_read_after_write(vulkan_buffer buf,
	VkAccessFlags read_kind)
{
	return (VkBufferMemoryBarrier){
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = read_kind,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.buffer = buf.handle,
		.offset = 0,
		.size = buf.size,
	};
}

void draw(context *ctx, attached_swapchain *sc,
	pipeline_layout *graphics_layout, VkPipeline gpipe,
	pipeline_layout *compute_layout, VkPipeline cpipe, VkPipeline cmdpipe,
	uploaded_mesh *mesh, camera *cam,
	vulkan_buffer instbuf, vulkan_buffer workbuf, vulkan_buffer drawbuf,
	float dt, orbit_tree *tree)
{
	// cpu wait for current frame to be out of graphics pipeline
	attached_swapchain_swap_buffers(ctx, sc);
	float now = (float) glfwGetTime();
	// render
	VkCommandBuffer cmd = attached_swapchain_current_graphics_cmd(sc);
	vkResetCommandBuffer(cmd, 0);
	command_buffer_begin(cmd);
	struct push_constant_data pushc;
	push_constant_populate(&pushc, cam, sc->frame_indx,
		now, dt, tree->height, tree->n_orbit);
	vkCmdPushConstants(cmd, compute_layout->handle,
		VK_SHADER_STAGE_COMPUTE_BIT |
		VK_SHADER_STAGE_VERTEX_BIT  |
		VK_SHADER_STAGE_FRAGMENT_BIT,
		0, sizeof(pushc), &pushc);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cpipe);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
		compute_layout->handle, 0, 1, compute_layout->set, 0, NULL);
	vkCmdDispatch(cmd, tree->n_orbit / LOCAL_SIZE, 1, 1);
	VkBufferMemoryBarrier cmd_barrier =
		barrier_read_after_write(workbuf, VK_ACCESS_SHADER_READ_BIT);
	vkCmdPipelineBarrier(cmd,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0,
		0, NULL,
		1, &cmd_barrier,
		0, NULL
	);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cmdpipe);
	vkCmdDispatch(cmd, CHUNK_COUNT, 1, 1);
	VkBufferMemoryBarrier barrier_desc[] = {
		barrier_read_after_write(instbuf, VK_ACCESS_SHADER_READ_BIT),
		barrier_read_after_write(workbuf, VK_ACCESS_SHADER_READ_BIT),
		barrier_read_after_write(drawbuf, VK_ACCESS_INDIRECT_COMMAND_READ_BIT),
	};
	vkCmdPipelineBarrier(cmd,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
		0,
		0, NULL,
		ARRAY_SIZE(barrier_desc), barrier_desc,
		0, NULL
	);
	VkClearValue clear[] = {
		[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}},
		[1].depthStencil = {0.0f, 0},
	};
	VkRenderPassBeginInfo pass_desc = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = sc->pass,
		.framebuffer = sc->framebuffer[sc->base.i_slot],
		.renderArea.offset = {0, 0},
		.renderArea.extent = sc->base.dim,
		.clearValueCount = ARRAY_SIZE(clear),
		.pClearValues = clear,
	};
	vkCmdBeginRenderPass(cmd, &pass_desc, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gpipe);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		graphics_layout->handle, 0,
		1, &graphics_layout->set[sc->frame_indx],
		0, NULL);
	vkCmdBindIndexBuffer(cmd, mesh->indx.handle, 0, VK_INDEX_TYPE_UINT32);
	vkCmdBindVertexBuffers(cmd, 0, 1, &mesh->vert.handle, &(VkDeviceSize){0});
	for (u32 lod = 0; lod < MAX_LOD; lod++) {
		pushc.lod = lod;
		vkCmdPushConstants(cmd, graphics_layout->handle,
			VK_SHADER_STAGE_COMPUTE_BIT |
			VK_SHADER_STAGE_VERTEX_BIT  |
			VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(pushc), &pushc);
		vkCmdDrawIndexedIndirect(cmd,
			drawbuf.handle,
			(sc->frame_indx * MAX_DRAW_PER_FRAME + lod) * sizeof(VkDrawIndexedIndirectCommand),
			MAX_DRAW_PER_FRAME / MAX_LOD,
			MAX_LOD * sizeof(VkDrawIndexedIndirectCommand)
		);
	}
	vkCmdEndRenderPass(cmd);
	command_buffer_end(cmd);
	// submitting commands for next frame
	VkSubmitInfo submission_desc = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = attached_swapchain_current_present_ready(sc),
		.pWaitDstStageMask = (VkPipelineStageFlags[]){
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
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
	loaded_image images[] = {
		load_image("res/2k_sun.jpg"),
		load_image("res/2k_ceres_fictional.jpg"),
		load_image("res/2k_eris_fictional.jpg"),
		load_image("res/2k_haumea_fictional.jpg"),
		load_image("res/2k_jupiter.jpg"),
		load_image("res/2k_makemake_fictional.jpg"),
		load_image("res/2k_mars.jpg"),
		load_image("res/2k_mercury.jpg"),
		load_image("res/2k_moon.jpg"),
		load_image("res/2k_neptune.jpg"),
		load_image("res/2k_saturn.jpg"),
		load_image("res/2k_uranus.jpg"),
		load_image("res/2k_venus_surface.jpg"),
	};
	vulkan_bound_image textures = vulkan_bound_image_upload(&ctx,
		ARRAY_SIZE(images), images, &loading_lifetime);
	lifetime_bind_image(&window_lifetime, textures);
	VkSampler sampler = sampler_create(&ctx);
	lifetime_bind_sampler(&window_lifetime, sampler);
	u32 vertsz = uv_sphere_vert_size(64, 48) + uv_sphere_vert_size(16, 12)
		   + uv_sphere_vert_size( 8,  4) + uv_sphere_vert_size( 3,  2);
	u32 indxsz = uv_sphere_indx_size(64, 48) + uv_sphere_indx_size(16, 12)
		   + uv_sphere_indx_size( 8,  4) + uv_sphere_indx_size( 3,  2);
	char *mesh_storage = xmalloc(vertsz + indxsz);
	char *mesh_indx = mesh_storage + vertsz;
	mesh m[4];
	m[0] = uv_sphere(64, 48, 0.5f, (void*) mesh_storage, (void*) mesh_indx);
	m[1] = uv_sphere(16, 12, 0.5f, m[0].vert + m[0].nvert, m[0].indx + m[0].nindx);
	m[2] = uv_sphere( 8,  4, 0.5f, m[1].vert + m[1].nvert, m[1].indx + m[1].nindx);
	m[3] = uv_sphere( 3,  2, 0.5f, m[2].vert + m[2].nvert, m[2].indx + m[2].nindx);
	// MUST BE CONTIGUOUS
	uploaded_mesh lods = mesh_upload(&ctx, ARRAY_SIZE(m), m,
		&loading_lifetime, &window_lifetime);
	free(mesh_storage);
	orbit_tree tree = orbit_tree_init(MAX_ITEMS_PER_FRAME - 1);
	assert(tree.n_orbit < MAX_ITEMS);
	vulkan_buffer orbit_spec = data_upload(&ctx,
		sizeof(struct orbit_spec), tree.uploading_orbit_specs,
		&loading_lifetime, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	lifetime_bind_buffer(&window_lifetime, orbit_spec);
	vulkan_buffer instbuf = buffer_create(&ctx,
		MAX_ITEMS * sizeof(mat4),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	lifetime_bind_buffer(&window_lifetime, instbuf);
	VkDrawIndexedIndirectCommand *drawmapped =
		xmalloc(MAX_DRAW * sizeof(*drawmapped));

	for (u32 i = 0, iframe = 0; iframe < MAX_FRAMES_RENDERING; iframe++) {
		for (u32 ichunk = 0; ichunk < MAX_DRAW_PER_FRAME / MAX_LOD; ichunk++) {
			for (u32 ilod = 0; ilod < MAX_LOD; ilod++) {
				drawmapped[i].indexCount =
					lods.ibase[ilod+1] - lods.ibase[ilod];
				drawmapped[i].instanceCount = 0;
				drawmapped[i].firstIndex = lods.ibase[ilod];
				drawmapped[i].vertexOffset = (i32) lods.vbase[ilod];
				drawmapped[i].firstInstance = 0;
				i++;
			}
		}
	}

	vulkan_buffer drawbuf = data_upload(&ctx,
		MAX_DRAW * sizeof(*drawmapped), drawmapped,
		&loading_lifetime,
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	free(drawmapped);
	lifetime_bind_buffer(&window_lifetime, drawbuf);
	vulkan_buffer workbuf = buffer_create(&ctx, 2 * MAX_ITEMS * sizeof(u32),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	lifetime_bind_buffer(&window_lifetime, workbuf);
	VkDescriptorSetLayoutBinding graphics_bind[] = {
		descset_layout_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
		descset_layout_binding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
		descset_layout_binding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
	};
	VkDescriptorPoolSize graphics_poolz[] = {
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES_RENDERING },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * MAX_FRAMES_RENDERING },
	};
	VkPushConstantRange pushc_desc = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT
			    | VK_SHADER_STAGE_FRAGMENT_BIT
			    | VK_SHADER_STAGE_COMPUTE_BIT,
		.offset = 0,
		.size = sizeof(struct push_constant_data),
	};
	void *graphics_binddesc[] = {
		&(VkDescriptorBufferInfo){ instbuf.handle, 0, instbuf.size },
		&(VkDescriptorBufferInfo){ workbuf.handle, 0, workbuf.size },
		&(VkDescriptorImageInfo ){ sampler, textures.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
	};
	pipeline_layout graphics_layout = pipeline_layout_create(ctx.device, MAX_FRAMES_RENDERING,
		ARRAY_SIZE(graphics_bind), graphics_bind, graphics_binddesc,
		ARRAY_SIZE(graphics_poolz), graphics_poolz,
		&pushc_desc);
	VkPipeline gpipe = graphics_pipeline_create("bin/shader.vert.spv", "bin/shader.frag.spv",
		ctx.device, sc.base.dim, sc.pass, 0, &graphics_layout);
	VkDescriptorSetLayoutBinding compute_bind[] = {
		descset_layout_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
		descset_layout_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
		descset_layout_binding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
		descset_layout_binding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
	};
	VkDescriptorPoolSize compute_poolz[] = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE , MAX_FRAMES_RENDERING },
	};
	void *compute_binddesc[] = {
		&(VkDescriptorBufferInfo){ orbit_spec.handle, 0, orbit_spec.size },
		&(VkDescriptorBufferInfo){ instbuf   .handle, 0, instbuf   .size },
		&(VkDescriptorBufferInfo){ workbuf   .handle, 0, workbuf   .size },
		&(VkDescriptorBufferInfo){ drawbuf   .handle, 0, drawbuf   .size },
	};
	pipeline_layout compute_layout = pipeline_layout_create(ctx.device, 1,
		ARRAY_SIZE(compute_bind), compute_bind, compute_binddesc,
		ARRAY_SIZE(compute_poolz), compute_poolz,
		&pushc_desc);
	VkPipeline cpipe = compute_pipeline_create("bin/update_models.comp.spv",
		ctx.device, &compute_layout);
	VkPipeline cmdpipe = compute_pipeline_create("bin/make_draws.comp.spv",
		ctx.device, &compute_layout);
	lifetime_fini(&loading_lifetime, &ctx);
	orbit_tree_fini(&tree);
	free(lods.vbase);

	float dt = 0.0f;
	camera cam = camera_init(sc.base.dim,
		(vec3){ 0.0f, -12.0f, 2.0f },
		(vec3){ 0.0f, 0.0f, 0.0f },
		ctx.window
	);
	context_ignore_mouse_once(&ctx);
	while (context_keep(&ctx)) {
		double beg_time = glfwGetTime();
		camera_update(&cam, &ctx, dt);
		camera_matrix(&cam);
		draw(&ctx, &sc,
			&graphics_layout, gpipe,
			&compute_layout, cpipe, cmdpipe,
			&lods, &cam,
			instbuf, workbuf, drawbuf,
			dt, &tree);
		double end_time = glfwGetTime();
		printf("\rframe time: %.2fms", (end_time - beg_time) * 1e3);
		dt = (float) (end_time - beg_time);
	}
	printf("\n");
	vkDeviceWaitIdle(ctx.device);

	vkDestroyPipeline(ctx.device, cmdpipe, NULL);
	vkDestroyPipeline(ctx.device, cpipe, NULL);
	pipeline_layout_destroy(ctx.device, &compute_layout);
	vkDestroyPipeline(ctx.device, gpipe, NULL);
	pipeline_layout_destroy(ctx.device, &graphics_layout);
	lifetime_fini(&window_lifetime, &ctx);
	attached_swapchain_destroy(&ctx, &sc);
	context_fini(&ctx);
	return 0;
}

