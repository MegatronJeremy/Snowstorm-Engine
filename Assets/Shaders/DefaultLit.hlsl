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
	// Tangent to world space (same rigid/affine assumption); keep handedness w untouched.
	o.TangentWS = float4(normalize(mul(i.Tangent.xyz, (float3x3)model)), i.Tangent.w);

	o.TexCoord = i.TexCoord;
	o.PositionCS = mul(posWS, ViewProj);
	o.InstanceID = iid;
	return o;
}

#type fragment

static const float PI = 3.14159265359;

// Sample a bindless texture by a (potentially per-instance, non-uniform) index. Every dynamic index
// into the Textures[] array must go through NonUniformResourceIndex() or instanced draws sample
// garbage (the #46 flicker lesson) — centralize it here so no call site forgets.
float4 SampleBindless(uint index, float2 uv)
{
	return Textures[NonUniformResourceIndex(index)].Sample(LinearSampler, uv);
}

// --- Cook-Torrance terms ---
float DistributionGGX(float3 N, float3 H, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float NdotH = max(dot(N, H), 0.0);
	float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
	return a2 / max(PI * d * d, 1e-5);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
	// Direct-lighting remap of roughness (Disney/UE4).
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;
	return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
	return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// World-space shading normal: perturb the geometric normal by the tangent-space normal map when one
// is bound, otherwise fall back to the interpolated vertex normal.
float3 ResolveNormal(PSInput i, uint normalIndex)
{
	float3 N = normalize(i.NormalWS);
	if (normalIndex == 0)
	{
		return N;
	}
	float3 T = normalize(i.TangentWS.xyz);
	// Re-orthogonalize (Gram-Schmidt) so interpolation skew doesn't tilt the basis.
	T = normalize(T - N * dot(N, T));
	float3 B = cross(N, T) * i.TangentWS.w; // handedness sign baked at import
	float3 sampled = SampleBindless(normalIndex, i.TexCoord).xyz * 2.0 - 1.0; // [0,1] -> [-1,1]
	float3x3 TBN = float3x3(T, B, N);
	return normalize(mul(sampled, TBN));
}

float4 main(PSInput i) : SV_Target0
{
	// Per-instance albedo override: a non-zero per-instance index wins over the material default,
	// so objects sharing one material can each show a different texture (and still batch).
	const uint instAlbedo = Instances[i.InstanceID].AlbedoTextureIndex;
	const uint albedoIndex = (instAlbedo != 0) ? instAlbedo : AlbedoTextureIndex;
	const float3 albedo = (albedoIndex != 0 ? SampleBindless(albedoIndex, i.TexCoord).rgb : float3(1, 1, 1)) * BaseColor.rgb;

	// Metallic-roughness from the packed MR texture (glTF: G = roughness, B = metallic) * factors.
	float roughness = Roughness;
	float metallic = Metallic;
	if (MetallicRoughnessTextureIndex != 0)
	{
		float3 mr = SampleBindless(MetallicRoughnessTextureIndex, i.TexCoord).rgb;
		roughness *= mr.g;
		metallic *= mr.b;
	}
	roughness = clamp(roughness, 0.04, 1.0); // avoid a zero-area specular lobe

	const float ao = (AOTextureIndex != 0) ? SampleBindless(AOTextureIndex, i.TexCoord).r : 1.0;

	const float3 N = ResolveNormal(i, NormalTextureIndex);
	const float3 V = normalize(CameraPosition - i.PositionWS);
	const float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

	float3 Lo = float3(0, 0, 0);
	const int count = clamp(LightCount, 0, MAX_DIRECTIONAL_LIGHTS);
	[loop] for (int l = 0; l < count; ++l)
	{
		const float3 L = normalize(-DirectionalLights[l].Direction);
		const float3 H = normalize(V + L);
		const float NdotL = max(dot(N, L), 0.0);
		const float3 radiance = DirectionalLights[l].Color * DirectionalLights[l].Intensity;

		const float D = DistributionGGX(N, H, roughness);
		const float G = GeometrySmith(N, V, L, roughness);
		const float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

		const float3 specular = (D * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 1e-4);
		const float3 kd = (1.0 - F) * (1.0 - metallic); // metals have no diffuse
		Lo += (kd * albedo / PI + specular) * radiance * NdotL;
	}

	// Hemisphere ambient stopgap until image-based lighting (#52, needs the skybox #35): a sky/ground
	// gradient by world-up gives metals a directional environment to reflect instead of reading black.
	const float3 skyColor = float3(0.30, 0.34, 0.42);
	const float3 groundColor = float3(0.18, 0.16, 0.14);
	const float3 ambientEnv = lerp(groundColor, skyColor, N.y * 0.5 + 0.5);
	const float3 ambient = ambientEnv * albedo * ao;

	float3 color = Lo + ambient;

	// Emissive (sRGB-sampled) + scalar factor.
	if (EmissiveTextureIndex != 0)
	{
		color += SampleBindless(EmissiveTextureIndex, i.TexCoord).rgb * EmissiveColor;
	}
	else
	{
		color += EmissiveColor;
	}

	return float4(color, BaseColor.a);
}