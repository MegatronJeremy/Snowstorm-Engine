// Spatial reuse for ReSTIR GI: combine each pixel's reservoir with a few neighbours' and resolve to radiance.
//
// This is the half that makes the algorithm pay. A reservoir carries ONE sample whose variance does not fall
// with M, so resampling and temporal reuse on their own leave a single-sample-per-pixel estimate whose noise is
// temporally correlated (measured: both are worse than plain averaging). Borrowing decorrelated neighbours is
// what turns that back into an effectively-many-sample estimate.
//
// A neighbour is a genuinely DIFFERENT visible point, so its sample must be reweighted by the reconnection
// Jacobian (Ouyang et al., HPG 2021, sec. 4.3). Temporal reuse could skip this because motion vectors target the
// same world point; here the geometry actually changes and omitting it biases every reused sample.

Texture2D<float4> GBufferNormal : register(t0, space0); // full-res: .xy = oct world normal
Texture2D<float> GBufferDepth : register(t1, space0);   // full-res fp32 NDC depth
Texture2D<float4> ResSample : register(t2, space0);     // .xyz sample pos, .w W
Texture2D<float4> ResRadiance : register(t3, space0);   // .xyz radiance, .w M
Texture2D<float4> ResNormal : register(t4, space0);     // .xy oct sample normal, .z linear view depth
[[vk::image_format("rgba16f")]] RWTexture2D<float4> GIOut : register(u5, space0);

// Set 3: the engine bindless pool, declared exactly as RTHitShading.hlsli does so the reflected layout matches
// what BindGlobalResources binds. That header itself is not included here: it references the GI pass's hit-light
// cbuffer block, which this pass has no use for.
Texture2D Textures[] : register(t0, space3);
TextureCube Cubemaps[] : register(t1, space3);
RaytracingAccelerationStructure SceneTLAS : register(t2, space3);

cbuffer SpatialCB : register(b6, space0)
{
	float4x4 InvViewProj;
	uint2 OutSize;       // half-res reservoir/output dimensions
	float GIIntensity;
	uint FrameCounter;

	float NearPlane;
	float FarPlane;
	float SpatialRadius; // neighbour search radius, in half-res pixels
	uint SpatialCount;   // neighbours sampled per pixel

	uint CheckVisibility; // trace the reconnection ray instead of assuming the sample is visible from here
	uint3 _Pad;
};

#include "Include/GBufferEncode.hlsli"

// Rejecting a neighbour costs one sample; accepting a mismatched one biases the pixel, so both tests are strict.
static const float kNormalRejectDot = 0.9;
static const float kDepthRejectRel = 0.05;
// The Jacobian diverges as the reused sample approaches grazing at either endpoint. Clamping trades a little
// bias for bounded variance, which is the standard tradeoff: unclamped, one near-degenerate reconnection
// produces a firefly that survives every downstream denoiser.
static const float kMaxReuseWeight = 8.0;

float LinearizeDepth(const float d)
{
	return (NearPlane * FarPlane) / max(FarPlane - d * (FarPlane - NearPlane), 1e-6);
}

float Luma(const float3 c)
{
	return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float Hash01(uint3 p)
{
	uint n = p.x * 1597334673u ^ p.y * 3812015801u ^ p.z * 2654435761u;
	n = (n ^ (n >> 16)) * 2246822519u;
	n = (n ^ (n >> 13)) * 3266489917u;
	return float(n ^ (n >> 16)) * (1.0 / 4294967296.0);
}

float3 WorldFromDepth(const float2 uv, const float d)
{
	const float2 ndc = uv * 2.0 - 1.0;
	const float4 worldH = mul(float4(ndc, d, 1.0), InvViewProj);
	return worldH.xyz / worldH.w;
}

// Point-fetch the full-res G-buffer at the texel nearest this half-res pixel's centre. A bilinear tap would
// blend depth across a silhouette and reconstruct a world position in mid-air.
bool LoadSurface(const float2 uv, out float3 positionWS, out float3 normalWS, out float linearDepth)
{
	uint2 gbDims;
	GBufferNormal.GetDimensions(gbDims.x, gbDims.y);
	const int2 texel = clamp(int2(uv * float2(gbDims)), int2(0, 0), int2(gbDims) - 1);

	const float d = GBufferDepth.Load(int3(texel, 0)).r;
	positionWS = 0.0;
	normalWS = 0.0;
	linearDepth = 0.0;
	if (IsSky(d))
	{
		return false;
	}
	positionWS = WorldFromDepth(uv, d);
	normalWS = DecodeNormalOct(GBufferNormal.Load(int3(texel, 0)).xy);
	linearDepth = LinearizeDepth(d);
	return true;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= OutSize.x || id.y >= OutSize.y)
	{
		return;
	}

	const float2 uv = (float2(id.xy) + 0.5) / float2(OutSize);

	float3 centrePos;
	float3 centreNormal;
	float centreLinDepth;
	if (!LoadSurface(uv, centrePos, centreNormal, centreLinDepth))
	{
		GIOut[id.xy] = float4(0, 0, 0, 0);
		return;
	}

	// Seed the combined reservoir with this pixel's own, which needs no reweighting (reuse factor 1).
	const float4 ownRad = ResRadiance.Load(int3(id.xy, 0));
	const float4 ownSample = ResSample.Load(int3(id.xy, 0));
	float3 selRadiance = ownRad.xyz;
	float selReuse = 1.0;
	float resM = max(ownRad.w, 0.0);
	float resWSum = Luma(selRadiance) * max(ownSample.w, 0.0) * resM;

	for (uint i = 0; i < SpatialCount; ++i)
	{
		// Uniform disk offset; sqrt keeps the samples area-uniform rather than clustered at the centre.
		const float r1 = Hash01(uint3(id.xy, FrameCounter * 64u + i * 2u));
		const float r2 = Hash01(uint3(id.xy, FrameCounter * 64u + i * 2u + 1u));
		const float radius = SpatialRadius * sqrt(r1);
		const float angle = r2 * 6.2831853;
		const int2 nPx = int2(id.xy) + int2(round(float2(cos(angle), sin(angle)) * radius));

		if (nPx.x < 0 || nPx.y < 0 || nPx.x >= int(OutSize.x) || nPx.y >= int(OutSize.y))
		{
			continue;
		}
		if (nPx.x == int(id.x) && nPx.y == int(id.y))
		{
			continue;
		}

		const float2 nUv = (float2(nPx) + 0.5) / float2(OutSize);
		float3 nPos;
		float3 nNormal;
		float nLinDepth;
		if (!LoadSurface(nUv, nPos, nNormal, nLinDepth))
		{
			continue;
		}

		// Same-surface test. The Jacobian corrects geometry, not a change of surface: a neighbour across an edge
		// carries radiance for a different orientation entirely.
		if (dot(nNormal, centreNormal) < kNormalRejectDot)
		{
			continue;
		}
		if (abs(nLinDepth - centreLinDepth) / max(centreLinDepth, 1e-4) > kDepthRejectRel)
		{
			continue;
		}

		const float4 nRad = ResRadiance.Load(int3(nPx, 0));
		const float4 nSample = ResSample.Load(int3(nPx, 0));
		if (nRad.w <= 0.0 || nSample.w <= 0.0)
		{
			continue;
		}
		const float3 sampleNormal = DecodeNormalOct(ResNormal.Load(int3(nPx, 0)).xy);
		const float3 samplePos = nSample.xyz;

		// Reconnection: the same sample point seen from the neighbour's visible point and from ours.
		const float3 toSampleN = samplePos - nPos;
		const float3 toSampleC = samplePos - centrePos;
		const float dN = length(toSampleN);
		const float dC = length(toSampleC);
		if (dN < 1e-4 || dC < 1e-4)
		{
			continue;
		}
		const float3 dirN = toSampleN / dN;
		const float3 dirC = toSampleC / dC;

		const float cosThetaN = dot(nNormal, dirN);
		const float cosThetaC = dot(centreNormal, dirC);
		if (cosThetaN <= 1e-4 || cosThetaC <= 1e-4)
		{
			continue; // sample sits below one of the two horizons, so it contributes nothing there
		}
		const float cosPhiN = abs(dot(sampleNormal, -dirN));
		const float cosPhiC = abs(dot(sampleNormal, -dirC));

		// Jacobian of the solid-angle measure change, times the cosine ratio that converts the neighbour's
		// cosine-weighted density to ours. Together these are the factor that makes the reused sample an
		// unbiased estimate AT THIS pixel.
		const float jacobian = (cosPhiC * dN * dN) / max(cosPhiN * dC * dC, 1e-6);
		const float reuse = min((cosThetaC / cosThetaN) * jacobian, kMaxReuseWeight);

		// The sample was only ever verified visible from the NEIGHBOUR. Depth and normal agreeing says the two
		// pixels share a surface, not that they share an unoccluded path to the sample point, so without this
		// the reconnection leaks light around occluders.
		if (CheckVisibility != 0)
		{
			RayDesc shadowRay;
			shadowRay.Origin = centrePos + centreNormal * 1e-3;
			shadowRay.Direction = dirC;
			shadowRay.TMin = 1e-3;
			shadowRay.TMax = dC - 2e-3;
			if (shadowRay.TMax > shadowRay.TMin)
			{
				RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> vq;
				vq.TraceRayInline(SceneTLAS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, shadowRay);
				vq.Proceed();
				if (vq.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
				{
					continue; // alpha-masked geometry counts as opaque here, which over-occludes rather than leaks
				}
			}
		}

		const float w = Luma(nRad.xyz) * reuse * nSample.w * nRad.w;
		if (w <= 0.0)
		{
			continue;
		}
		resWSum += w;
		resM += nRad.w;
		if (Hash01(uint3(id.xy, FrameCounter * 64u + 63u - i)) < w / resWSum)
		{
			selRadiance = nRad.xyz;
			selReuse = reuse;
		}
	}

	// The reuse factor cancels out of the final estimate algebraically, but it is NOT redundant: it set the
	// selection probabilities and the weight sum above.
	const float target = Luma(selRadiance) * selReuse;
	const float W = (target > 0.0 && resM > 0.0) ? (resWSum / (resM * target)) : 0.0;
	GIOut[id.xy] = float4(selRadiance * selReuse * W * GIIntensity, 1.0);
}
