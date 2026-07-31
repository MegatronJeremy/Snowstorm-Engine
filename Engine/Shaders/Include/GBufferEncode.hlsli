// GBufferEncode.hlsli — the single source of truth for the depth+normal G-buffer's packing (#129 Inc 1b).
// Shared by the producer (DepthNormal.frag) and every consumer (GI / Reflection / AO compute + the
// GI/AO bilateral upsamples + the GI denoiser) so the layout is defined ONCE and can't drift.
//
// Layout (one RGBA16F color image, sampled as a plain color — NOT the depth-stencil attachment):
//   .xy = octahedral-encoded world-space SHADING normal (normal-mapped, matching DefaultLit) in [-1,1]
//   .z  = perceptual roughness [0,1] (for the reflection trace-skip / future glossy blur)
//   .w  = NDC depth (SV_Position.z, ZO clip [0,1]); 1.0 = far plane / sky
//
// Sky/no-geometry detection keys off DEPTH, not the normal: the target is cleared to depth = 1.0, and a
// real far-plane fragment is also ~1.0, so `depth >= 1.0` means "nothing here". This replaced the old
// zero-length-normal test, which octahedral encoding breaks (oct(0,0) decodes to a VALID +Z normal, so a
// zero .xy no longer means "sky"). Every consumer must use IsSky(depth), never dot(N,N).

#ifndef SNOWSTORM_GBUFFER_ENCODE_HLSLI
#define SNOWSTORM_GBUFFER_ENCODE_HLSLI

// --- Octahedral normal encoding (Cigolle et al. 2014, "A Survey of Efficient Representations for
// Independent Unit Vectors"). Maps a unit vector to [-1,1]^2 and back with ~equal-area distortion — the
// standard deferred-G-buffer normal pack. Two channels instead of three, and it round-trips a unit normal
// to < 0.5 degrees error at fp16, far tighter than the reflection needs. ---
// sign() with the standard graphics convention that sign(0) = +1 (HLSL's sign() returns 0 at 0), so a
// component exactly on an axis maps consistently. Avoids the vector ternary HLSL 2021 forbids.
float2 OctSignNZ(float2 v)
{
	return float2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

float2 OctWrap(float2 v)
{
	return (1.0 - abs(v.yx)) * OctSignNZ(v.xy);
}

// Encode a unit world normal -> oct [-1,1]^2.
float2 EncodeNormalOct(float3 n)
{
	n /= (abs(n.x) + abs(n.y) + abs(n.z));
	n.xy = n.z >= 0.0 ? n.xy : OctWrap(n.xy);
	return n.xy;
}

// Decode oct [-1,1]^2 -> unit world normal.
float3 DecodeNormalOct(float2 e)
{
	float3 n = float3(e.xy, 1.0 - abs(e.x) - abs(e.y));
	const float t = saturate(-n.z);
	n.xy += OctSignNZ(n.xy) * (-t);
	return normalize(n);
}

// True when this G-buffer texel is sky / no geometry (cleared depth, or the far plane).
bool IsSky(float depth)
{
	return depth >= 1.0;
}

// Convenience packers so producer + debug reads agree on channel order.
float4 PackGBuffer(float3 normalWS, float roughness, float ndcDepth)
{
	return float4(EncodeNormalOct(normalWS), roughness, ndcDepth);
}

#endif // SNOWSTORM_GBUFFER_ENCODE_HLSLI
