#pragma once

// Tangent-space normal-map decoding, shared by every shader that samples one.
//
// This header declares NO resources on purpose. The three consumers (DefaultLit, DepthNormal, PathTrace)
// do not share a descriptor layout, and a resource declared in a shared header is emitted into every
// shader that includes it, so putting this in Engine.hlsli would push bindings into two pipelines that
// have never carried them.

// Decode a sampled normal-map texel to a [-1,1] vector.
//
// BC5-encoded normals carry only X and Y, so the sampler returns B = 0 and Z must be reconstructed. A
// stored tangent-space normal always has z > 0, which encodes above 0.5, so a raw blue below that is
// unambiguously the two-channel format and never a legitimate three-channel value. saturate() guards the
// sqrt: quantization can push x^2 + y^2 slightly past 1, and the resulting NaN would propagate through
// lighting. The path tracer decodes through this too, or the reference and the real-time paths would
// disagree about the same texture and every quality baseline would shift.
float3 DecodeTangentNormal(float3 raw)
{
	if (raw.z < 0.5)
	{
		float2 xy = raw.xy * 2.0 - 1.0;
		return float3(xy, sqrt(saturate(1.0 - dot(xy, xy))));
	}
	return raw * 2.0 - 1.0;
}
