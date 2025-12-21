#type vertex

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

#type fragment

struct PSInput
{
    float4 PositionCS : SV_Position;
    float2 TexCoord   : TEXCOORD0;
};

float3 hsv_to_rgb(float h, float s, float v)
{
    float f = frac(h) * 6.0;
    int i = (int)floor(f);
    f -= i;

    float p = v * (1.0 - s);
    float q = v * (1.0 - s * f);
    float t = v * (1.0 - s * (1.0 - f));

    if (i == 0) return float3(v, t, p);
    if (i == 1) return float3(q, v, p);
    if (i == 2) return float3(p, v, t);
    if (i == 3) return float3(p, q, v);
    if (i == 4) return float3(t, p, v);
    return float3(v, p, q);
}

float4 main(PSInput i) : SV_Target0
{
    const float2 center = Extras0.xy;
    const float zoom = Extras0.z;
    const int maxIter = max(1, (int)Extras0.w);

    // Map texcoord to complex plane
    const float2 c = center + (i.TexCoord - 0.5) * zoom;

    float2 z = float2(0.0, 0.0);
    int iter = 0;

    [loop]
    for (iter = 0; iter < maxIter; ++iter)
    {
        if (dot(z, z) > 4.0) break;
        z = float2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
    }

    const float colorFactor = (float)iter / (float)maxIter;
    const float3 color = hsv_to_rgb(colorFactor, 1.0, 1.0);

    if (iter == maxIter)
        return float4(0.0, 0.0, 0.0, 1.0);

    return float4(color, 1.0);
}