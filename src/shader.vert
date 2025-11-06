#version 450

#include "shared.h"

// uniforms
layout(push_constant) uniform draw_data {
	push_constant_data draw;
};

// in attributes
layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

// out attributes
layout(location = 0) out vec3 vert_normal;
layout(location = 1) out vec2 vert_uv;

void main()
{
	gl_Position = draw.pos_tfm * vec4(pos, 1.0);
	vert_uv = uv;
	mat3 norm_tfm = mat3(draw.norm_tfm);
	vert_normal = norm_tfm * normal;
}

