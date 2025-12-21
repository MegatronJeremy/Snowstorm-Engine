
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
};

cbuffer FrameCB : register(b0, space0)
{
    float4x4 ViewProj;
    // keep layout compatible with DefaultLit FrameCB for now (optional)
    float3   _PadCam;
    float    _Pad0;
};

cbuffer ObjectCB : register(b0, space2)
{
    float4x4 Model;
    float4   Extras0; // Center.xy, Zoom, MaxIterations(float)
};

VSOutput main(VSInput i)
{
    VSOutput o;
    o.TexCoord = i.TexCoord;

    const float4 posWS = mul(Model, float4(i.Position, 1.0));
    o.PositionCS = mul(ViewProj, posWS);
    return o;
}

