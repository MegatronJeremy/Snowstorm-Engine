// Depth+normal prepass, fragment stage (#124, upgraded #129 Inc 1b). Writes the G-buffer as ONE RGBA16F
// color target, packed per GBufferEncode.hlsli: .xy = octahedral NORMAL-MAPPED world normal, .z =
// perceptual roughness, .w = NDC depth (SV_Position.z). Packing into a plain COLOR image (not the
// depth-stencil attachment) matters: a depth-stencil image in a shader lives in DEPTH_STENCIL_READ_ONLY
// layout, which a compute sampled-image descriptor (expecting SHADER_READ_ONLY) rejects; a color image
// transitions cleanly. The D32 attachment is still present for the prepass's own z-test (+ forward
// early-z), just never sampled.
//
// #129 Inc 1c: the prepass is now MRT and writes TWO normals, because ray occlusion/GI and reflections
// want DIFFERENT normals (the standard deferred split — cf. Unreal Lumen / NRD, which keep both):
//   SV_Target0 (main G-buffer): .xy = octahedral GEOMETRIC normal, .z = roughness, .w = NDC depth. AO/GI
//     orient their sample hemisphere off this — the geometric normal keeps the hemisphere flush on the true
//     surface, so rays don't dip below a bumped normal and self-occlude (the AO darkening #129 Inc 1b caused).
//   SV_Target1 (shading-normal target): .xy = octahedral NORMAL-MAPPED normal. ONLY the reflection pass
//     reads this — reflections must reflect off the bumped normal to match DefaultLit's shading.
// Roughness rides target0.z for the reflection trace-skip / future glossy blur. Normal map + MR texture are
// sampled from bindless Textures[] (set 3) via push indices, with this pass's OWN sampler (set 1, binding 0)
// — NOT the material set (allocated against DefaultLit's set-1 layout; binding it here is a device loss).
//
// Alpha-mask clip (#124): cutout geometry (glTF MASK) must punch holes in the G-buffer too, or the RT
// passes reconstruct phantom solid surfaces where the texture is transparent.

#include "Include/GBufferEncode.hlsli"

struct DepthNormalPush
{
	float4x4 ViewProj;
	uint AlbedoTextureIndex;
	uint AlphaMaskEnabled;
	float AlphaCutoff;
	float BaseAlpha;

	uint NormalTextureIndex;
	float Roughness;
	uint MetallicRoughnessTextureIndex;
	uint _Pad0;
};
[[vk::push_constant]] DepthNormalPush gDN;

// This pass's own sampler (set 1, binding 0) — NOT the material set. Clamp-linear is fine for these taps.
SamplerState AlbedoSampler : register(s0, space1);

// Bindless 2D textures (set 3) — the same array DefaultLit samples; indices come via the push.
Texture2D Textures[] : register(t0, space3);

struct DepthNormalVSOut
{
	float4 PositionCS : SV_Position;
	float3 NormalWS : TEXCOORD0;
	float4 TangentWS : TEXCOORD1;
	float2 TexCoord : TEXCOORD2;
};

// Normal mapping — mirrors DefaultLit's ResolveNormal (TBN from the interpolated normal+tangent, Gram-
// Schmidt re-orthogonalized, sampled tangent-space normal [0,1]->[-1,1]). Geometric normal when no map.
float3 ResolveShadingNormal(float3 nWS, float4 tangentWS, float2 uv)
{
	const float3 N = normalize(nWS);
	if (gDN.NormalTextureIndex == 0)
	{
		return N;
	}
	float3 T = normalize(tangentWS.xyz);
	T = normalize(T - N * dot(N, T)); // re-orthogonalize so interpolation skew doesn't tilt the basis
	const float3 B = cross(N, T) * tangentWS.w;
	const float3 sampled = Textures[NonUniformResourceIndex(gDN.NormalTextureIndex)].Sample(AlbedoSampler, uv).xyz * 2.0 - 1.0;
	const float3x3 TBN = float3x3(T, B, N);
	return normalize(mul(sampled, TBN));
}

struct DepthNormalOut
{
	float4 Main : SV_Target0;    // .xy oct GEOMETRIC normal, .z roughness, .w NDC depth
	float4 Shading : SV_Target1; // .xy oct NORMAL-MAPPED normal (reflection pass only); .zw unused
};

DepthNormalOut main(DepthNormalVSOut input)
{
	// Alpha-mask cutout: discard transparent texels BEFORE writing, so a cutout leaf leaves a hole.
	if (gDN.AlphaMaskEnabled != 0 && gDN.AlbedoTextureIndex != 0)
	{
		const float alpha = Textures[NonUniformResourceIndex(gDN.AlbedoTextureIndex)].Sample(AlbedoSampler, input.TexCoord).a * gDN.BaseAlpha;
		clip(alpha - gDN.AlphaCutoff);
	}

	const float3 nGeom = normalize(input.NormalWS);                                        // geometric (ray effects)
	const float3 nShade = ResolveShadingNormal(input.NormalWS, input.TangentWS, input.TexCoord); // normal-mapped (reflections)

	// Per-pixel roughness: material scalar * MR-texture .g (glTF packing), matching DefaultLit. Clamp to the
	// same floor so a mirror still reads a tiny non-zero roughness (consumers may branch on it).
	float roughness = gDN.Roughness;
	if (gDN.MetallicRoughnessTextureIndex != 0)
	{
		roughness *= Textures[NonUniformResourceIndex(gDN.MetallicRoughnessTextureIndex)].Sample(AlbedoSampler, input.TexCoord).g;
	}
	roughness = clamp(roughness, 0.04, 1.0);

	DepthNormalOut o;
	o.Main = PackGBuffer(nGeom, roughness, input.PositionCS.z); // .xy geometric, .z roughness, .w depth
	o.Shading = float4(EncodeNormalOct(nShade), 0.0, 0.0);      // .xy shading normal (reflection pass)
	return o;
}
