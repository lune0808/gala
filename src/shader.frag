#version 450

// in attributes
layout(location = 0) in vec3 vert_normal;
layout(location = 1) in vec2 vert_uv;

// attachments
layout(location = 0) out vec4 frag_color;

// uniforms
layout(binding = 1) uniform sampler2D tex;

void main()
{
	vec4 color = vec4(texture(tex, vert_uv).rgb, 1.0);
	frag_color = color;
}

