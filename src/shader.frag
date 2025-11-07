#version 450

#include "shared.h"

// uniforms
layout(binding = 1) uniform sampler2D tex;
layout(push_constant) uniform draw_data {
	push_constant_data draw;
};

// varyings
layout(location = 0) in vec3 vert_world_pos;
layout(location = 1) in vec3 vert_normal;
layout(location = 2) in vec2 vert_uv;

// attachments
layout(location = 0) out vec4 frag_color;

void main()
{
	vec3 color = texture(tex, vert_uv).rgb;
	vec3 source = draw.normalmat[3].xyz;
	vec3 normal = normalize(vert_normal);
	vec3 to_light = -normalize(source - vert_world_pos);
	float diffuse_scale = dot(to_light, normal);
	vec3 diffuse = diffuse_scale * color;
	frag_color = vec4(diffuse, 1.0);
}

