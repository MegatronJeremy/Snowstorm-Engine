// Specular prefilter: GGX importance-sample the environment cubemap into a roughness-blurred cubemap.
// Dispatched once per (mip, face); each mip corresponds to a roughness level (mip 0 = mirror, higher
// mips = rougher). Split-sum specular IBL (Karis / LearnOpenGL). Third stage of the IBL bake (#52).

#type compute

static const float PI = 3.14159265359;

cbuffer PrefilterParams : register(b0, space0)
{
	float Roughness; // roughness for this mip
	uint FaceIndex;  // which face this dispatch writes (0..5)
	float _p0;
	float _p1;
}

TextureCube EnvCube : register(t1, space0);
SamplerState EnvSampler : register(s2, space0);
// rgba16f matches the IBL cube texture format (RGBA16_SFloat); see IBLCapture.hlsl for why this is explicit.
[[vk::image_format("rgba16f")]] RWTexture2D<float4> OutFace : register(u3, space0);

float3 CubeFaceDirection(uint face, float2 uv)
{
	float3 dir;
	switch (face)
	{
	case 0: dir = float3(1.0, -uv.y, -uv.x); break;
	case 1: dir = float3(-1.0, -uv.y, uv.x); break;
	case 2: dir = float3(uv.x, 1.0, uv.y); break;
	case 3: dir = float3(uv.x, -1.0, -uv.y); break;
	case 4: dir = float3(uv.x, -uv.y, 1.0); break;
	default: dir = float3(-uv.x, -uv.y, -1.0); break;
	}
	return normalize(dir);
}

// Van der Corput / Hammersley low-discrepancy sequence.
float RadicalInverseVdC(uint bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint n)
{
	return float2(float(i) / float(n), RadicalInverseVdC(i));
}

// GGX importance sample: a half-vector around N for the given roughness.
float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
	const float a = roughness * roughness;
	const float phi = 2.0 * PI * Xi.x;
	const float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
	const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

	const float3 H = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

	float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
	const float3 tangent = normalize(cross(up, N));
	const float3 bitangent = cross(N, tangent);
	return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	uint width, height;
	OutFace.GetDimensions(width, height);
	if (id.x >= width || id.y >= height)
	{
		return;
	}

	const float2 uv = (float2(id.xy) + 0.5) / float2(width, height) * 2.0 - 1.0;
	const float3 N = CubeFaceDirection(FaceIndex, uv);
	const float3 R = N;
	const float3 V = N; // split-sum assumption: V = R = N

	const uint SAMPLE_COUNT = 256u;
	float3 prefiltered = float3(0, 0, 0);
	float totalWeight = 0.0;

	[loop] for (uint i = 0u; i < SAMPLE_COUNT; ++i)
	{
		const float2 Xi = Hammersley(i, SAMPLE_COUNT);
		const float3 H = ImportanceSampleGGX(Xi, N, Roughness);
		const float3 L = normalize(2.0 * dot(V, H) * H - V);

		const float NdotL = max(dot(N, L), 0.0);
		if (NdotL > 0.0)
		{
			prefiltered += EnvCube.SampleLevel(EnvSampler, L, 0).rgb * NdotL;
			totalWeight += NdotL;
		}
	}

	prefiltered = prefiltered / max(totalWeight, 1e-4);
	OutFace[id.xy] = float4(prefiltered, 1.0);
}
