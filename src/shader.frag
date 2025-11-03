#version 450 core

layout(location = 0) in vec3 vert_color;
layout(location = 1) in vec2 vert_uv;

layout(location = 0) out vec4 frag_color;

layout(binding = 1) uniform sampler2D tex;

void main()
{
	frag_color = vec4(0.01*vert_color + texture(tex, vert_uv).rgb, 1.0);
}

