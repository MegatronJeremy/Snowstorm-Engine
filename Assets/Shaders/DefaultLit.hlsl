#include "Engine.hlsli"

#type vertex

VSOutput main(VSInput i, uint iid : SV_InstanceID)
{
	VSOutput o;

	const float4x4 model = Instances[iid].Model;

	const float4 posWS = mul(float4(i.Position, 1.0), model);
	o.PositionWS = posWS.xyz;

	// Normal matrix: for now, treat Model as rigid/affine (same as your GLSL mat3(model))
	o.NormalWS = normalize(mul(i.Normal, (float3x3)model));

	o.TexCoord = i.TexCoord;
	o.PositionCS = mul(posWS, ViewProj);
	o.InstanceID = iid;
	return o;
}

#type fragment

float3 ComputeLighting(float3 normal, float3 albedo)
{
	float3 result = albedo * 0.05; // ambient

	const int count = clamp(LightCount, 0, MAX_DIRECTIONAL_LIGHTS);
	[loop] for (int i = 0; i < count; ++i)
	{
		float3 lightDir = normalize(-DirectionalLights[i].Direction);
		float lambert = max(dot(normal, lightDir), 0.0);
		result += albedo * DirectionalLights[i].Color * lambert * DirectionalLights[i].Intensity;
	}

	return result;
}

float4 main(PSInput i) : SV_Target0
{
	// Per-instance albedo override: a non-zero per-instance index wins over the material default,
	// so objects sharing one material can each show a different texture (and still batch).
	const uint instAlbedo = Instances[i.InstanceID].AlbedoTextureIndex;
	const uint albedoIndex = (instAlbedo != 0) ? instAlbedo : AlbedoTextureIndex;
	const float3 albedo = Textures[albedoIndex].Sample(LinearSampler, i.TexCoord).rgb * BaseColor.rgb;
	const float3 normal = normalize(i.NormalWS);
	const float3 lit = ComputeLighting(normal, albedo);
	return float4(lit, BaseColor.a);
}