#version 450

layout(location = 0) out vec2 uv;

void main()
{
	vec2 pos = vec2(2.0 * (gl_VertexIndex & 1), gl_VertexIndex & 2);
	uv = pos;
	gl_Position = vec4(pos, 0.0, 1.0);
}

