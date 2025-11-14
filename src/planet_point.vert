#version 450

#include "shared.h"

// uniforms
layout(push_constant) uniform info_data {
	push_constant_data info;
};

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
	vert_uv = vec2(0.5, 0.5);
	mat4 model = pull.model[imodel[gl_VertexIndex]];
	vert_texindex = model[3].w;
	model[0].w = 0.0;
	model[1].w = 0.0;
	model[2].w = 0.0;
	model[3].w = 1.0;
	vec4 pos = model * vec4(0.0, 0.0, 0.0, 1.0);
	vert_world_pos = pos.xyz;
	vert_normal = info.cam_pos.xyz - pos.xyz;
	gl_Position = info.viewproj * pos;
	gl_PointSize = 1.0;
}

