
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

