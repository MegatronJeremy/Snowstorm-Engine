// Neural TEMPORAL upscaler input stage (#98): motion-vector reprojection of the previous frame's output
// into the current frame, plus the raw motion vector, written into the feature map as extra input channels.
// This is the DLSS/XeSS substrate: the net sees {bilinear LR (ch0-2, from NeuralUpsampleIn), MV-warped
// previous output (ch3-5), motion vector (ch6-7)} and learns per-pixel how much to trust reprojected
// history vs. reject it (disocclusion) — the thing classic TAA's fixed neighborhood-clamp does poorly.
//
// Runs AFTER NeuralUpsampleIn (which filled ch0-2) and BEFORE the conv stack, on the same flat CHW feature
// buffer. The warp is deterministic engine code so it is IDENTICAL to the Python training-side warp
// (torch grid_sample, bilinear) — no train/inference drift (the #102 lesson).
//
// BILINEAR reprojection (not Catmull-Rom): the Python side must match this exactly for training, and
// F.grid_sample(mode='bilinear') is a 1:1 match for a linear-clamp SampleLevel. A higher-quality
// Catmull-Rom warp is a deliberate later upgrade (would need a matching Python implementation).

Texture2D<float4> PrevHistory : register(t0, space0); // previous frame's full-res output (RGBA16F)
Texture2D<float4> Velocity : register(t1, space0);    // full-res motion vectors (.xy = curr_uv - prev_uv)
SamplerState LinearClamp : register(s2, space0);
RWStructuredBuffer<float> OutMap : register(u3, space0); // CHW feature buffer; this stage writes ch3..7

cbuffer WarpCB : register(b4, space0)
{
	uint2 OutSize; // full-res feature-map W,H
	uint HistoryValid; // 0 on the first temporal frame / after a resize -> no valid history yet
	uint _Pad;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= OutSize.x || id.y >= OutSize.y)
	{
		return;
	}

	const float2 uv = (float2(id.xy) + 0.5f) / float2(OutSize);

	// Velocity points current -> previous (curr_uv - prev_uv), so the previous-frame location is uv - velocity
	// (same convention as TemporalResolve). Sampled with integer Load at the current pixel (velocity is full-res
	// and pixel-aligned here).
	const float2 vel = Velocity.Load(int3(id.xy, 0)).xy;
	const float2 histUv = uv - vel;

	// Warp the previous output into this frame. Disocclusion: if the reprojected UV leaves the frame, there is
	// no valid history for this pixel -> feed zeros (the net learns to fall back to the LR input). Same guard on
	// the very first temporal frame, where PrevHistory holds nothing meaningful yet.
	float3 warped = float3(0.0f, 0.0f, 0.0f);
	const bool inBounds = all(histUv >= 0.0f) && all(histUv <= 1.0f);
	if (HistoryValid != 0 && inBounds)
	{
		warped = PrevHistory.SampleLevel(LinearClamp, histUv, 0).rgb;
	}

	const uint plane = OutSize.x * OutSize.y;
	const uint pix = id.y * OutSize.x + id.x;
	OutMap[3 * plane + pix] = warped.r; // channel 3: warped history R
	OutMap[4 * plane + pix] = warped.g; // channel 4: warped history G
	OutMap[5 * plane + pix] = warped.b; // channel 5: warped history B
	OutMap[6 * plane + pix] = vel.x;    // channel 6: motion vector x
	OutMap[7 * plane + pix] = vel.y;    // channel 7: motion vector y
}
