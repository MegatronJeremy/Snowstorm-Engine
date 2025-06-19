#type vertex
#version 450 core

layout(location = 0) in vec3 a_Position;
out vec3 v_TexCoord;

uniform mat4 u_View;
uniform mat4 u_Projection;

void main()
{
	vec4 pos = u_View * vec4(a_Position, 1.0);
	gl_Position = u_Projection * pos;
	v_TexCoord = a_Position; // don't transform this
}

#type fragment
#version 450 core

in vec3 v_TexCoord;
out vec4 FragColor;

uniform samplerCube u_Skybox;

void main()
{
	FragColor = texture(u_Skybox, normalize(v_TexCoord));
}