#version 450

#include "shared.h"

// uniforms
layout(push_constant) uniform draw_data {
	push_constant_data draw;
};

// attributes
layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in mat4 model_and_data;

// varyings
layout(location = 0) out vec3 vert_world_pos;
layout(location = 1) out vec3 vert_normal;
layout(location = 2) out vec2 vert_uv;
layout(location = 3) out float vert_texindex;

void main()
{
	vert_uv = uv;
	mat4 model = model_and_data;
	vert_texindex = model[3].w;
	model[0].w = 0.0;
	model[1].w = 0.0;
	model[2].w = 0.0;
	model[3].w = 1.0;
	mat3 normalmat = mat3(model);
	normalmat = transpose(inverse(normalmat));
	vert_normal = normalmat * normal;
	vert_world_pos = (model * vec4(pos, 1.0)).xyz;
	gl_Position = draw.viewproj * model * vec4(pos, 1.0);
}

