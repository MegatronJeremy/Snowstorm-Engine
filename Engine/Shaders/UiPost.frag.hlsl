#include "Include/Engine.hlsli"
#include "Include/UiPost.hlsli"

// Every colour here is premultiplied, as the UI layers hold it.

float4 Source(float2 uv)
{
	return Textures[NonUniformResourceIndex(gPost.Texture)].SampleLevel(UiPostSampler, gPost.SampleOffset + uv * gPost.SampleScale, 0);
}

float4 Filtered(UiPostVSOut input)
{
	const float2 uv = input.UV;
	switch (gPost.Mode)
	{
	case UI_POST_OPACITY:
		return Source(uv) * gPost.Colour.a;

	case UI_POST_COLOR_MATRIX:
	{
		const float4 c = Source(uv);
		const float3 straight = c.a > 0.0 ? c.rgb / c.a : float3(0.0, 0.0, 0.0);
		const float3 m = gPost.MatrixR.xyz * straight.r + gPost.MatrixG.xyz * straight.g + gPost.MatrixB.xyz * straight.b +
		                 float3(gPost.MatrixR.w, gPost.MatrixG.w, gPost.MatrixB.w);
		return float4(saturate(m) * c.a, c.a);
	}

	case UI_POST_BLUR:
	{
		// Weights from the Gaussian itself, normalised by their own sum, so a truncated tail loses no energy.
		// The caller downsamples first when sigma is large, which keeps this under 3 sigma of 12 taps.
		const float sigma = max(gPost.Sigma, 1e-3);
		const int radius = min((int)ceil(3.0 * sigma), 36);
		float4 sum = 0.0;
		float weights = 0.0;
		[loop] for (int i = -radius; i <= radius; ++i)
		{
			const float w = exp(-0.5 * (i * i) / (sigma * sigma));
			sum += Source(uv + gPost.TexelStep * i) * w;
			weights += w;
		}
		return sum / weights;
	}

	case UI_POST_SHADOW:
		return gPost.Colour * Source(uv - gPost.ShadowOffset).a;

	case UI_POST_OVER:
	{
		const float4 shadow = Source(uv);
		const float4 original = Textures[NonUniformResourceIndex(gPost.Texture2)].SampleLevel(UiPostSampler, uv, 0);
		return original + shadow * (1.0 - original.a);
	}

	case UI_POST_MASK_MULTIPLY:
	{
		const float m = Source(uv).r * Textures[NonUniformResourceIndex(gPost.Texture2)].SampleLevel(UiPostSampler, uv, 0).r;
		return m.xxxx;
	}

	default:
		return Source(uv);
	}
}

float4 main(UiPostVSOut input) : SV_Target
{
	float4 colour = Filtered(input);
	if (gPost.Mask != 0)
	{
		colour *= Textures[NonUniformResourceIndex(gPost.Mask)].Load(int3(input.PositionCS.xy, 0)).r;
	}
	return colour;
}
