#include "Engine.hlsli"

// DefaultLit fragment stage: metallic-roughness PBR (Cook-Torrance) + normal mapping + directional
// shadows + split-sum IBL, then exposure/ACES tonemap/sRGB encode. Paired with the shared
// Mesh.vert.hlsl. Was the fragment half of the old combined DefaultLit.hlsl.

static const float PI = 3.14159265359;

// Sample a bindless texture by a (potentially per-instance, non-uniform) index. Every dynamic index
// into the Textures[] array must go through NonUniformResourceIndex() or instanced draws sample
// garbage (the #46 flicker lesson) — centralize it here so no call site forgets.
float4 SampleBindless(uint index, float2 uv)
{
	return Textures[NonUniformResourceIndex(index)].Sample(LinearSampler, uv);
}

// Directional-sun shadow factor: 1 = fully lit, 0 = fully shadowed. Reprojects the world position into
// light clip space, then does a 3x3 PCF compare against the stored shadow-map depth. Manual PCF keeps
// the existing bindless SAMPLED_IMAGE model (no comparison-sampler descriptor). NdotL drives a
// slope-scaled bias to kill acne on grazing surfaces; ShadowStrength lets shadows be lightened.
float SampleSunShadow(float3 positionWS, float NdotL)
{
	if (ShadowMapIndex == 0)
	{
		return 1.0; // no shadow map bound
	}

	float4 lightClip = mul(float4(positionWS, 1.0), LightViewProj);
	float3 ndc = lightClip.xyz / lightClip.w;

	// Outside the light frustum (depth or XY) => treat as lit, don't darken unshadowed areas.
	if (ndc.z > 1.0 || ndc.z < 0.0)
	{
		return 1.0;
	}

	// Clip XY [-1,1] -> UV [0,1]. NO Y-flip: the engine's SetViewport does NOT apply the Vulkan
	// negative-height flip (despite its comment), so the whole renderer is internally consistent in
	// un-flipped clip space. Flipping here would mismatch the shadow map's own rasterization.
	float2 uv = ndc.xy * float2(0.5, 0.5) + 0.5;
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
	{
		return 1.0;
	}

	// Depth bias: a constant floor plus a slope-scaled term (more bias on surfaces grazing the light).
	// The floor matters even for light-facing surfaces (shadow-map texel quantization causes acne there
	// too); a pure slope term collapses to ~0 at high NdotL and lets acne through.
	const float bias = ShadowBias + ShadowBias * 4.0 * (1.0 - NdotL);

	const float currentDepth = ndc.z - bias;

	// Soft (3x3 PCF, averaged compares) or hard (single tap), per the render.shadow.soft quality setting.
	float visibility;
	if (ShadowSoft != 0)
	{
		float sum = 0.0;
		[unroll] for (int dy = -1; dy <= 1; ++dy)
		{
			[unroll] for (int dx = -1; dx <= 1; ++dx)
			{
				const float2 offset = float2(dx, dy) * ShadowTexelSize;
				const float storedDepth = SampleBindless(ShadowMapIndex, uv + offset).r;
				sum += (currentDepth <= storedDepth) ? 1.0 : 0.0;
			}
		}
		visibility = sum / 9.0;
	}
	else
	{
		const float storedDepth = SampleBindless(ShadowMapIndex, uv).r;
		visibility = (currentDepth <= storedDepth) ? 1.0 : 0.0;
	}

	// ShadowStrength scales how dark shadows get (1 = full occlusion -> visibility, 0 = no shadowing).
	return lerp(1.0, visibility, ShadowStrength);
}

// ACES filmic tonemap (Narkowicz fit): compress unbounded linear HDR into [0,1] with a filmic
// shoulder/toe, so bright spots roll off instead of clipping flat white. Cheap, no LUT.
float3 TonemapACES(float3 x)
{
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Encode linear -> sRGB. The scene target is a UNORM image displayed as sRGB with no hardware
// encode, so we gamma-encode in the shader; without this the whole image reads too dark.
float3 LinearToSRGB(float3 c)
{
	return pow(max(c, 0.0), 1.0 / 2.2);
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

// Roughness-aware Fresnel for the ambient/IBL term (Sebastien Lagarde): rough surfaces shouldn't show
// a full grazing Fresnel boost the way a smooth one does.
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
	const float3 fMax = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0);
	return F0 + (fMax - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Split-sum image-based lighting: diffuse from the irradiance cube, specular from the prefiltered cube
// (roughness -> mip) modulated by the BRDF LUT. Returns 0 (caller falls back to analytic ambient) when
// IBL isn't baked (IrradianceCubeIndex == 0).
float3 ComputeIBL(float3 N, float3 V, float3 albedo, float3 F0, float roughness, float metallic, float ao)
{
	if (IrradianceCubeIndex == 0)
	{
		return float3(0, 0, 0);
	}

	const float NdotV = max(dot(N, V), 0.0);
	const float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
	const float3 kd = (1.0 - F) * (1.0 - metallic); // metals have no diffuse

	// Diffuse: irradiance (already cosine-convolved) * albedo.
	const float3 irradiance = Cubemaps[NonUniformResourceIndex(IrradianceCubeIndex)].SampleLevel(LinearSampler, N, 0).rgb;
	const float3 diffuse = irradiance * albedo;

	// Specular: prefiltered env at the reflection vector, lod from roughness; scaled by the BRDF LUT.
	const float3 R = reflect(-V, N);
	const float lod = roughness * float(max(PrefilteredMipCount, 1u) - 1u);
	const float3 prefiltered = Cubemaps[NonUniformResourceIndex(PrefilteredCubeIndex)].SampleLevel(LinearSampler, R, lod).rgb;

	// BRDF LUT (split-sum scale+bias), indexed by (NdotV, roughness). Sampled through ClampSampler (NOT the
	// wrapping LinearSampler): a LUT must clamp, or a bilinear tap at NdotV~1 wraps to the grazing edge and
	// produces a hard brightness seam down the middle of the view.
	const float2 brdf = Textures[NonUniformResourceIndex(BRDFLutIndex)].SampleLevel(ClampSampler, float2(NdotV, roughness), 0).rg;
	const float3 specular = prefiltered * (F0 * brdf.x + brdf.y);

	// Scale by the dedicated IBLIntensity dial (separate from SkyIntensity: the irradiance cube is
	// already cosine-convolved, a different scale than the analytic hemisphere lerp). Tune to taste.
	return (kd * diffuse + specular) * ao * IBLIntensity;
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
	float3 B = cross(N, T) * i.TangentWS.w;                                   // handedness sign baked at import
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

		// Only the primary sun (light 0) casts shadows in this single-map implementation; other
		// directionals are unshadowed. Ambient is unaffected, so shadowed areas stay lit-but-dim.
		const float shadow = (l == 0) ? SampleSunShadow(i.PositionWS, NdotL) : 1.0;

		Lo += (kd * albedo / PI + specular) * radiance * NdotL * shadow;
	}

	// Ambient: prefer split-sum IBL (baked from the sky) when available — metals reflect the environment
	// and specular picks up sky color. Falls back to the analytic hemisphere ambient (same zenith/horizon/
	// ground colors the sky shows) when IBL isn't baked, so the look degrades gracefully.
	float3 ambient = ComputeIBL(N, V, albedo, F0, roughness, metallic, ao);
	if (IrradianceCubeIndex == 0)
	{
		const float3 ambientEnv = (N.y >= 0.0)
		                              ? lerp(SkyHorizonColor, SkyZenithColor, saturate(N.y))
		                              : lerp(SkyHorizonColor, GroundColor, saturate(-N.y));
		ambient = ambientEnv * SkyIntensity * albedo * ao;
	}

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

	// Output transform: linear HDR -> exposure -> filmic tonemap -> sRGB encode. Without this the raw
	// linear BRDF result clips to flat white at highlights and reads too dark elsewhere on an sRGB
	// display. Exposure is the FrameCB knob (CVar render.exposure).
	color = TonemapACES(color * Exposure);
	color = LinearToSRGB(color);

	return float4(color, BaseColor.a);
}