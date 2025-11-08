#version 450

#include "shared.h"

// uniforms
layout(binding = 1) uniform sampler2DArray tex;
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
	mat4 nmat = draw.normalmat;
	float pr = nmat[0].w;
	float pg = nmat[1].w;
	float pb = nmat[2].w;
	vec3 color = texture(tex, vec3(vert_uv, draw.tex)).rgb;
	color = vec3(pow(color.r, pr), pow(color.g, pg), pow(color.b, pb));
	vec3 source = nmat[3].xyz;
	vec3 normal = normalize(vert_normal);
	vec3 to_light = normalize(source - vert_world_pos);
	float diffuse = pow(max(0.0, dot(to_light, normal)), 8.0);
	float ambient = nmat[3].w;
	frag_color = vec4((ambient + diffuse) * color, 1.0);
}

