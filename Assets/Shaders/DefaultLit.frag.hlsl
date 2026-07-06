#include "Include/Engine.hlsli"

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

// Shared shadow factor: 1 = fully lit, 0 = fully shadowed. Reprojects the world position through a light
// matrix, then does a 3x3 PCF compare against a bindless depth texture. `atlasRect` (xy = UV offset,
// zw = UV scale) maps the light's [0,1] UV into its sub-rect of the texture: (0,0,1,1) for a dedicated
// map (the sun), or a tile rect for a spot in the shared atlas. PCF taps are CLAMPED to the rect so a
// tap near a tile edge can't bleed into a neighbour tile. Manual PCF keeps the bindless SAMPLED_IMAGE
// model (no comparison-sampler descriptor). NdotL drives a slope-scaled bias; ShadowStrength lightens.
float SampleShadowFactor(uint texIndex, float4x4 lightViewProj, float4 atlasRect, float3 positionWS, float NdotL)
{
	float4 lightClip = mul(float4(positionWS, 1.0), lightViewProj);
	float3 ndc = lightClip.xyz / lightClip.w;

	// Behind the light or outside its depth range => treat as lit. (w<=0 guards points behind a
	// perspective spot, where the divide flips.)
	if (lightClip.w <= 0.0 || ndc.z > 1.0 || ndc.z < 0.0)
	{
		return 1.0;
	}

	// Clip XY [-1,1] -> light UV [0,1]. NO Y-flip: the engine's SetViewport does NOT apply the Vulkan
	// negative-height flip, so the whole renderer is internally consistent in un-flipped clip space.
	float2 lightUV = ndc.xy * 0.5 + 0.5;
	if (lightUV.x < 0.0 || lightUV.x > 1.0 || lightUV.y < 0.0 || lightUV.y > 1.0)
	{
		return 1.0; // outside this light's frustum footprint
	}

	// Depth bias: constant floor + slope-scaled term (more bias on surfaces grazing the light). The floor
	// matters even for light-facing surfaces (texel quantization causes acne there too).
	const float bias = ShadowBias + ShadowBias * 4.0 * (1.0 - NdotL);
	const float currentDepth = ndc.z - bias;

	// Atlas remap + tile clamp bounds (in atlas UV space). ShadowTexelSize is per full-texture texel;
	// scale by the rect so a tile's PCF step matches its sub-resolution.
	const float2 rectMin = atlasRect.xy;
	const float2 rectMax = atlasRect.xy + atlasRect.zw;

	float visibility;
	if (ShadowSoft != 0)
	{
		float sum = 0.0;
		[unroll] for (int dy = -1; dy <= 1; ++dy)
		{
			[unroll] for (int dx = -1; dx <= 1; ++dx)
			{
				const float2 tap = lightUV + float2(dx, dy) * ShadowTexelSize;
				const float2 atlasUV = clamp(atlasRect.xy + tap * atlasRect.zw, rectMin, rectMax);
				const float storedDepth = SampleBindless(texIndex, atlasUV).r;
				sum += (currentDepth <= storedDepth) ? 1.0 : 0.0;
			}
		}
		visibility = sum / 9.0;
	}
	else
	{
		const float2 atlasUV = clamp(atlasRect.xy + lightUV * atlasRect.zw, rectMin, rectMax);
		const float storedDepth = SampleBindless(texIndex, atlasUV).r;
		visibility = (currentDepth <= storedDepth) ? 1.0 : 0.0;
	}

	return lerp(1.0, visibility, ShadowStrength);
}

// Directional-sun shadow: dedicated map (full-texture rect), gated by ShadowMapIndex (0 = no shadows).
float SampleSunShadow(float3 positionWS, float NdotL)
{
	if (ShadowMapIndex == 0)
	{
		return 1.0;
	}
	return SampleShadowFactor(ShadowMapIndex, LightViewProj, float4(0, 0, 1, 1), positionWS, NdotL);
}

// Spot shadow: samples the shared atlas at the spot's tile. Gated by the atlas index being bound AND the
// spot having been assigned a tile (ShadowIndex >= 0).
float SampleSpotShadow(SpotLight spot, float3 positionWS, float NdotL)
{
	if (SpotShadowAtlasIndex == 0 || spot.ShadowIndex < 0)
	{
		return 1.0;
	}
	return SampleShadowFactor(SpotShadowAtlasIndex, spot.ShadowViewProj, spot.ShadowAtlasRect, positionWS, NdotL);
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
// Exact IEC 61966-2-1 piecewise curve: a linear toe (12.92 * c) below the threshold and a
// 1.055 * c^(1/2.4) - 0.055 shoulder above — not the pow(1/2.2) approximation, which lacks the
// linear segment and darkens deep shadows (#55). Deleted once a hardware-sRGB present path lands.
float3 LinearToSRGB(float3 c)
{
	c = max(c, 0.0);
	const float3 lo = c * 12.92;
	const float3 hi = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
	return lerp(hi, lo, step(c, 0.0031308));
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

// One light's Cook-Torrance contribution (diffuse + specular), given the already-normalized light
// direction L and the incoming radiance (color * intensity, pre-attenuation). Shared by the
// directional, point, and spot loops -- only how L/radiance are computed differs per light type.
float3 ShadePBR(float3 N, float3 V, float3 L, float3 F0, float3 albedo, float metallic, float roughness, float3 radiance)
{
	const float3 H = normalize(V + L);
	const float NdotL = max(dot(N, L), 0.0);

	const float D = DistributionGGX(N, H, roughness);
	const float G = GeometrySmith(N, V, L, roughness);
	const float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

	const float3 specular = (D * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 1e-4);
	const float3 kd = (1.0 - F) * (1.0 - metallic); // metals have no diffuse

	return (kd * albedo / PI + specular) * radiance * NdotL;
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
	const float4 albedoSample = (albedoIndex != 0) ? SampleBindless(albedoIndex, i.TexCoord) : float4(1, 1, 1, 1);

	// Alpha-cutout (glTF MASK): discard texels whose alpha (texture * BaseColor.a) is below the cutoff,
	// BEFORE any lighting so masked-out fragments cost nothing and don't write depth. clip() discards when
	// its argument is < 0. Opaque-pass masking only — no blending/sorting. Off (AlphaMaskEnabled == 0) for
	// normal materials, so this is a no-op there.
	if (AlphaMaskEnabled != 0)
	{
		clip(albedoSample.a * BaseColor.a - AlphaCutoff);
	}

	const float3 albedo = albedoSample.rgb * BaseColor.rgb;

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

	// --- Directional lights (the sun). Only light 0 casts shadows in this single-map implementation.
	const int count = clamp(LightCount, 0, MAX_DIRECTIONAL_LIGHTS);
	[loop] for (int l = 0; l < count; ++l)
	{
		const float3 L = normalize(-DirectionalLights[l].Direction);
		const float3 radiance = DirectionalLights[l].Color * DirectionalLights[l].Intensity;
		// Shadow multiplies the whole contribution; ambient is unaffected so shadows stay lit-but-dim.
		const float shadow = (l == 0) ? SampleSunShadow(i.PositionWS, max(dot(N, L), 0.0)) : 1.0;
		Lo += ShadePBR(N, V, L, F0, albedo, metallic, roughness, radiance) * shadow;
	}

	// --- Point lights: inverse-square falloff with a smooth windowed cutoff at Range (UE4/Frostbite).
	// Unshadowed. The window ((1-(d/R)^4)^2) drives the contribution to exactly zero at d==Range instead
	// of an abrupt clip, so there's no hard lit/unlit edge.
	const int pointCount = clamp(PointCount, 0, MAX_POINT_LIGHTS);
	[loop] for (int p = 0; p < pointCount; ++p)
	{
		const float3 toLight = PointLights[p].Position - i.PositionWS;
		const float dist = length(toLight);
		const float3 L = toLight / max(dist, 1e-4);

		const float range = max(PointLights[p].Range, 1e-4);
		const float window = pow(saturate(1.0 - pow(dist / range, 4.0)), 2.0);
		const float atten = window / max(dist * dist, 1e-4);

		const float3 radiance = PointLights[p].Color * PointLights[p].Intensity * atten;
		Lo += ShadePBR(N, V, L, F0, albedo, metallic, roughness, radiance);
	}

	// --- Spot lights: point attenuation multiplied by a smooth cone falloff between the inner/outer
	// half-angles (stored as cosines). -L is the light->surface direction compared to the spot's
	// forward axis. Unshadowed.
	const int spotCount = clamp(SpotCount, 0, MAX_SPOT_LIGHTS);
	[loop] for (int s = 0; s < spotCount; ++s)
	{
		const float3 toLight = SpotLights[s].Position - i.PositionWS;
		const float dist = length(toLight);
		const float3 L = toLight / max(dist, 1e-4);

		const float range = max(SpotLights[s].Range, 1e-4);
		const float window = pow(saturate(1.0 - pow(dist / range, 4.0)), 2.0);
		const float atten = window / max(dist * dist, 1e-4);

		// Cone falloff: 1 inside the inner angle, smoothly to 0 at the outer angle. cos() decreases with
		// angle, so a larger dot() == closer to the axis == more lit.
		const float cosAngle = dot(-L, SpotLights[s].Direction);
		const float denom = max(SpotLights[s].CosInner - SpotLights[s].CosOuter, 1e-4);
		const float cone = pow(saturate((cosAngle - SpotLights[s].CosOuter) / denom), 2.0);

		// Shadow: 1 when unshadowed / this spot casts no shadow. NdotL uses the surface normal vs L.
		const float spotShadow = SampleSpotShadow(SpotLights[s], i.PositionWS, max(dot(N, L), 0.0));

		const float3 radiance = SpotLights[s].Color * SpotLights[s].Intensity * atten * cone;
		Lo += ShadePBR(N, V, L, F0, albedo, metallic, roughness, radiance) * spotShadow;
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