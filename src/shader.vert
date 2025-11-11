#version 460

#include "shared.h"

// uniforms
layout(push_constant) uniform info_data {
	push_constant_data info;
};

// attributes
layout(location = 0) in vec3 attr_pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(std430, set = 0, binding = 1) readonly restrict buffer orbit_tfm {
	mat4 model[MAX_ITEMS];
} pull;

layout(std430, set = 0, binding = 2) readonly restrict buffer instance_indices {
	uint partial[MAX_ITEMS];
	uint imodel[MAX_ITEMS];
};

// varyings
layout(location = 0) out vec3 vert_world_pos;
layout(location = 1) out vec3 vert_normal;
layout(location = 2) out vec2 vert_uv;
layout(location = 3) out float vert_texindex;

void main()
{
	vert_uv = uv;
	mat4 model = pull.model[imodel[gl_InstanceIndex]];
	vert_texindex = model[3].w;
	model[0].w = 0.0;
	model[1].w = 0.0;
	model[2].w = 0.0;
	model[3].w = 1.0;
	mat3 normalmat = mat3(model);
	normalmat = transpose(inverse(normalmat));
	vert_normal = normalmat * normal;
	vec3 pos = attr_pos;
	pos.z += float(gl_DrawID) * 0.2;
	vert_world_pos = (model * vec4(pos, 1.0)).xyz;
	gl_Position = info.viewproj * model * vec4(pos, 1.0);
}

