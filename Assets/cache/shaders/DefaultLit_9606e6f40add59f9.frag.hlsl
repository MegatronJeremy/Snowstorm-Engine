
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
