// Engine.hlsli - The Unified Pipeline Layout

// --- SPACE 0: Global Frame Data ---
struct DirectionalLight
{
    float3 Direction;
    float  Intensity;
    float3 Color;
    float  Padding;
};

struct VSInput
{
    float3 Position : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
};

struct VSOutput
{
    float4 PositionCS : SV_Position;
    float2 TexCoord   : TEXCOORD0;
    float3 NormalWS   : TEXCOORD1;
    float3 PositionWS : TEXCOORD2;
};

struct PSInput
{
    float4 PositionCS : SV_Position;
    float2 TexCoord   : TEXCOORD0;
    float3 NormalWS   : TEXCOORD1;
    float3 PositionWS : TEXCOORD2;
};

static const int MAX_DIRECTIONAL_LIGHTS = 4;

cbuffer FrameCB : register(b0, space0) {
    float4x4 ViewProj;
    float3 CameraPosition;
    float _Pad0;
    DirectionalLight DirectionalLights[4];
    int LightCount;
    float3 _Pad1;
};

// --- SPACE 1: Material Data ---
cbuffer MaterialCB : register(b0, space1) {
    float4 BaseColor;
    uint   AlbedoTextureIndex;
    uint   NormalTextureIndex;
    float  Roughness;
    float  Metallic;
};
SamplerState LinearSampler : register(s1, space1);

// --- SPACE 2: Object Data ---
cbuffer ObjectCB : register(b0, space2) {
    float4x4 Model;
    float4   Extras0; 
};

// --- SPACE 3: Global Bindless Pool ---
Texture2D Textures[] : register(t0, space3);
