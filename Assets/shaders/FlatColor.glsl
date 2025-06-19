#type vertex
#version 450 core

layout(std140, binding = 0) uniform CameraData
{
    mat4 u_ViewProjection;
};

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in mat4 a_ModelMatrix;

out vec2 v_TexCoord;

void main()
{
    v_TexCoord = a_TexCoord;
	
    gl_Position = u_ViewProjection * a_ModelMatrix * vec4(a_Position, 1.0);
}

#type fragment
#version 450 core

in vec2 v_TexCoord;

layout(location = 0) out vec4 FragColor;

uniform sampler2D u_Textures[32];
uniform vec4 u_Color;

void main()
{
    FragColor = texture(u_Textures[0], v_TexCoord) * u_Color; 
}
