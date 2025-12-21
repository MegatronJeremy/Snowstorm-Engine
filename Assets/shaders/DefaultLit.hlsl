#type vertex

struct VSInput
{
    float3 Position : TEXCOORD0; // maps from location 0
    float3 Normal   : TEXCOORD1; // maps from location 1
    float2 TexCoord : TEXCOORD2; // maps from location 2
};

struct VSOutput
{
    float4 PositionCS : SV_Position;
    float2 TexCoord   : TEXCOORD0;
    float3 NormalWS   : TEXCOORD1;
    float3 PositionWS : TEXCOORD2;
};

// Set 0: Frame
// Note: pack camera + lights here for now (since set0 currently has only one UBO binding)
struct DirectionalLight
{
    float3 Direction;
    float  Intensity;
    float3 Color;
    float  Padding;
};

static const int MAX_DIRECTIONAL_LIGHTS = 4;

cbuffer FrameCB : register(b0, space0)
{
    float4x4 ViewProj;
    float3   CameraPosition;
    float    _Pad0;

    DirectionalLight DirectionalLights[MAX_DIRECTIONAL_LIGHTS];
    int LightCount;
    float3 _Pad1;
};

// Set 2: Object (dynamic)
cbuffer ObjectCB : register(b0, space2)
{
    float4x4 Model;
    float4   Extras0;
};

// Set 1: Material
cbuffer MaterialCB : register(b0, space1)
{
    float4 BaseColor;
};

VSOutput main(VSInput i)
{
    VSOutput o;

    const float4 posWS = mul(Model, float4(i.Position, 1.0));
    o.PositionWS = posWS.xyz;

    // Normal matrix: for now, treat Model as rigid/affine (same as your GLSL mat3(model))
    o.NormalWS = normalize(mul((float3x3)Model, i.Normal));

    o.TexCoord = i.TexCoord;
    o.PositionCS = mul(ViewProj, posWS);
    return o;
}

#type fragment

struct PSInput
{
    float4 PositionCS : SV_Position;
    float2 TexCoord   : TEXCOORD0;
    float3 NormalWS   : TEXCOORD1;
    float3 PositionWS : TEXCOORD2;
};

// Texture model for HLSL (separate SRV + Sampler)
// This REQUIRES your set=1 layout to have:
// - binding 1: SampledImage[32]  (t0..t31 in space1)
// - binding 2: Sampler           (s0 in space1)   OR sampler array (see note below)
Texture2D    Textures[32] : register(t0, space1);
SamplerState LinearSampler : register(s0, space1);

float3 ComputeLighting(float3 normal, float3 albedo)
{
    float3 result = albedo * 0.05; // ambient

    const int count = clamp(LightCount, 0, MAX_DIRECTIONAL_LIGHTS);
    [loop]
    for (int i = 0; i < count; ++i)
    {
        float3 lightDir = normalize(-DirectionalLights[i].Direction);
        float lambert = max(dot(normal, lightDir), 0.0);
        result += albedo * DirectionalLights[i].Color * lambert * DirectionalLights[i].Intensity;
    }

    return result;
}

float4 main(PSInput i) : SV_Target0
{
    const float3 albedo = Textures[0].Sample(LinearSampler, i.TexCoord).rgb * BaseColor.rgb;
    const float3 normal = normalize(i.NormalWS);
    const float3 lit = ComputeLighting(normal, albedo);
    return float4(lit, BaseColor.a);
}