#type vertex
#version 450 core

layout(std140, binding = 0) uniform CameraData
{
    mat4 u_ViewProjection;
    vec3 u_CameraPosition;
};

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in mat4 a_ModelMatrix;

out vec2 v_TexCoord;
out vec3 v_NormalWS;
out vec3 v_PositionWS;

void main()
{
    v_TexCoord = a_TexCoord;
    v_NormalWS = normalize(mat3(a_ModelMatrix) * a_Normal);
    v_PositionWS = vec3(a_ModelMatrix * vec4(a_Position, 1.0));

    gl_Position = u_ViewProjection * a_ModelMatrix * vec4(a_Position, 1.0);
}

#type fragment
#version 450 core

in vec2 v_TexCoord;
in vec3 v_NormalWS;
in vec3 v_PositionWS;

layout(location = 0) out vec4 FragColor;

uniform sampler2D u_Textures[32];
uniform vec4 u_Color;

struct DirectionalLight
{
    vec3 Direction;
    float Intensity;
    vec3 Color;
    float Padding;
};

#define MAX_DIRECTIONAL_LIGHTS 4
layout(std140, binding = 1) uniform LightData
{
    DirectionalLight u_DirectionalLights[MAX_DIRECTIONAL_LIGHTS];
    int u_LightCount;
};

vec3 ComputeLighting(vec3 normal, vec3 albedo)
{
    vec3 result = albedo * 0.05; // Ambient

    for (int i = 0; i < u_LightCount; ++i)
    {
        vec3 lightDir = normalize(-u_DirectionalLights[i].Direction);
        float lambert = max(dot(normal, lightDir), 0.0);
        result += albedo * u_DirectionalLights[i].Color * lambert * u_DirectionalLights[i].Intensity;
    }

    return result;
}

void main()
{
    vec3 albedo = texture(u_Textures[0], v_TexCoord).rgb * u_Color.rgb;
    vec3 normal = normalize(v_NormalWS);
    vec3 lighting = ComputeLighting(normal, albedo);

    FragColor = vec4(lighting, u_Color.a);
}
