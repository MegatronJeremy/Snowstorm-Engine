// BRDF integration LUT: precompute the environment-BRDF scale+bias (the split-sum's second factor)
// into a 2D RG texture, indexed by (NdotV, roughness). Sampled once per pixel in the lit shader to
// reconstruct specular IBL. Computed once (independent of the environment). Stage 4 of the IBL bake
// (#52). (Karis / LearnOpenGL integrateBRDF.)

#type compute

static const float PI = 3.14159265359;

// rgba16f matches the BRDF LUT texture format (RGBA16_SFloat); see IBLCapture.hlsl for why this is explicit.
[[vk::image_format("rgba16f")]] RWTexture2D<float4> OutLut : register(u0, space0);

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

// Smith geometry for IBL (note the k uses the IBL remap, not the direct-lighting one).
float GeometrySchlickGGX(float NdotV, float roughness)
{
	const float k = (roughness * roughness) / 2.0;
	return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
	return GeometrySchlickGGX(max(dot(N, L), 0.0), roughness) *
	       GeometrySchlickGGX(max(dot(N, V), 0.0), roughness);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	uint width, height;
	OutLut.GetDimensions(width, height);
	if (id.x >= width || id.y >= height)
	{
		return;
	}

	// x axis = NdotV, y axis = roughness (avoid the exact 0 edge).
	const float NdotV = max((float(id.x) + 0.5) / float(width), 1e-3);
	const float roughness = (float(id.y) + 0.5) / float(height);

	const float3 V = float3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
	const float3 N = float3(0.0, 0.0, 1.0);

	float A = 0.0;
	float B = 0.0;

	const uint SAMPLE_COUNT = 1024u;
	[loop] for (uint i = 0u; i < SAMPLE_COUNT; ++i)
	{
		const float2 Xi = Hammersley(i, SAMPLE_COUNT);
		const float3 H = ImportanceSampleGGX(Xi, N, roughness);
		const float3 L = normalize(2.0 * dot(V, H) * H - V);

		const float NdotL = max(L.z, 0.0);
		const float NdotH = max(H.z, 0.0);
		const float VdotH = max(dot(V, H), 0.0);

		if (NdotL > 0.0)
		{
			const float G = GeometrySmith(N, V, L, roughness);
			const float G_Vis = (G * VdotH) / (NdotH * NdotV);
			const float Fc = pow(1.0 - VdotH, 5.0);
			A += (1.0 - Fc) * G_Vis;
			B += Fc * G_Vis;
		}
	}

	OutLut[id.xy] = float4(A / float(SAMPLE_COUNT), B / float(SAMPLE_COUNT), 0.0, 1.0);
}
