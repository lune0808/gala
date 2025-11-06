#version 450

#include "shared.h"

// uniforms
layout(push_constant) uniform draw_data {
	mat4 tfm;
} draw;

// in attributes
layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 col;
layout(location = 2) in vec2 uv;

// out attributes
layout(location = 0) out vec3 vert_color;
layout(location = 1) out vec2 vert_uv;

void main()
{
	gl_Position = draw.tfm * vec4(pos, 1.0);
	vert_color = col;
	vert_uv = uv;
}

