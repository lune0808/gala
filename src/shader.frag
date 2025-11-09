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
layout(location = 3) in float vert_texindex;

// attachments
layout(location = 0) out vec4 frag_color;

void main()
{
#if 0
	vec3 red = vec3(1.0, 0.0, 0.0);
	vec3 green = vec3(0.0, 1.0, 0.0);
	frag_color = vec4(mix(red, green, draw.lod / 4.0), 1.0);
#else
	vec3 color = texture(tex, vec3(vert_uv, vert_texindex)).rgb;
	vec3 source = vec3(0.0);
	vec3 normal = normalize(vert_normal);
	vec3 to_light = normalize(source - vert_world_pos);
	float diffuse = pow(max(0.0, dot(to_light, normal)), 8.0);
	float ambient = 0.02f;
	frag_color = vec4((ambient + diffuse) * color, 1.0);
#endif
}

