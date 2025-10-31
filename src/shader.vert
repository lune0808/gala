#version 450 core

// uniform buffer
layout(binding = 0) uniform UBO {
	mat4 model;
	mat4 view;
	mat4 proj;
} transforms;

// in attributes
layout(location = 0) in vec2 pos;
layout(location = 1) in vec3 col;

// out attributes
layout(location = 0) out vec3 vert_color;

void main()
{
	gl_Position = transforms.proj
		    * transforms.view
		    * transforms.model
		    * vec4(pos, 0.0, 1.0);
	vert_color = col;
}

