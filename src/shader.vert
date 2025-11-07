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

// varyings
layout(location = 0) out vec3 vert_world_pos;
layout(location = 1) out vec3 vert_normal;
layout(location = 2) out vec2 vert_uv;

void main()
{
	vert_uv = uv;
	mat3 normalmat = mat3(draw.normalmat);
	vert_normal = normalmat * normal;
	vert_world_pos = (draw.model * vec4(pos, 1.0)).xyz;
	gl_Position = draw.mvp * vec4(pos, 1.0);
}

