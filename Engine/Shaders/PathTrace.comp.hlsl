// Reference path tracer, compute stage (#153). The ground-truth mode the thesis measures every real-time
// technique (SSAO/SSR/GI/reflections + their denoisers) against: a brute-force unidirectional path tracer
// with next-event estimation to the sun, a metallic-roughness microfacet BSDF (GGX specular + Lambert
// diffuse), multi-bounce indirect, Russian roulette, and progressive accumulation into a persistent HDR
// buffer while the camera is static. NOT real-time: this converges over many frames and is the correctness
// anchor, not a shipped renderer path.
//
// Environment: on a ray miss we add the analytic sky WITHOUT the sun disk (EvaluateSky with toSun = 0). The
// sun itself is a delta directional light handled EXACTLY by NEE (a shadow ray) at every hit, so gathering it
// from the environment too would double-count it and add huge variance (few continuation rays hit the tiny
// disk). Emissive surfaces are added on hit. Indirect light (GI, reflections, AO) all emerge from the
// multi-bounce BSDF-sampled continuation, so this is the reference for ALL of those at once.
//
// Set 0 = this pass's own resources; set 3 = bindless Textures/Cubemaps/SceneTLAS (gap-filled). Compiled only
// in the SS_RAYTRACING permutation (RayQuery). Mirrors the GI/Reflection compute passes' plumbing; the full
// per-hit material comes from the #153 PBR block appended to the geometry table (GeoRecord).

static const float PI = 3.14159265359;
static const float INV_PI = 0.31830988618;

// ---- Set 0 ----
[[vk::image_format("rgba32f")]] RWTexture2D<float4> AccumOut : register(u0, space0); // running MEAN radiance in .rgb
SamplerState LinearSampler : register(s2, space0);

cbuffer PTCB : register(b1, space0)
{
	float4x4 InvViewProj; // clip -> world (primary ray reconstruction)
	float3 CameraPosition;
	float SunCosThetaMax; // cos of the sun's angular RADIUS (finite disk for NEE); 1 = delta (hot-dot risk)

	uint2 OutSize;         // full-res dispatch dimensions
	uint BaseSampleCount;  // samples already accumulated before this frame (0 on reset)
	uint SamplesPerFrame;  // new paths this dispatch

	uint MaxBounces;
	uint Reset;            // 1 = first frame after a camera/scene change (overwrite, don't blend)
	uint LightCount;       // 0 = no sun
	uint FrameCounter;     // decorrelates the RNG seed across frames

	float3 SunDirection;   // world-space direction the light points (FROM the sun)
	float SunIntensity;
	float3 SunColor;
	float ShadowStrength;

	float3 SkyZenithColor;
	float LightSourceRadius; // point/spot light physical radius (finite size for NEE); 0 = delta (hot-dot risk)
	float3 SkyHorizonColor;
	float MaxBounceWeight; // path regularization: max per-bounce BSDF weight (0 = off); render.pathtrace.weightclamp
	float3 GroundColor;
	float FireflyClamp; // per-sample radiance clamp (0 = unbounded); render.pathtrace.clamp

	uint ReflGeoTableAddrLo; // geometry-table device address (lo/hi)
	uint ReflGeoTableAddrHi;
	uint PointCount; // # valid point lights below
	uint SpotCount;  // # valid spot lights below

	// Positional lights for NEE, world space, raw-packed into float4 rows (avoids struct cbuffer packing
	// surprises). Point: [2i] = xyz position, w range; [2i+1] = xyz color, w intensity. Max 16.
	float4 PointLights[32];
	// Spot: [4i] = pos.xyz, range; [4i+1] = color.xyz, intensity; [4i+2] = dir.xyz, cosInner; [4i+3].x = cosOuter. Max 16.
	float4 SpotLights[64];
};

static const uint MAX_PT_POINT_LIGHTS = 16u;
static const uint MAX_PT_SPOT_LIGHTS = 16u;

// Smooth inverse-square attenuation with a UE-style range cutoff (windowed so it reaches 0 at Range).
float PositionalAttenuation(float dist, float range)
{
	float atten = 1.0 / max(dist * dist, 1e-4);
	if (range > 0.0)
	{
		const float t = saturate(1.0 - pow(dist / range, 4.0));
		atten *= t * t;
	}
	return atten;
}

// Set 3 bindless + geometry-table decode + cutout any-hit. Textures[] must precede the RTGeometry include.
Texture2D Textures[] : register(t0, space3);
TextureCube Cubemaps[] : register(t1, space3);
RaytracingAccelerationStructure SceneTLAS : register(t2, space3);
#include "Include/RTGeometry.hlsli"
#include "Include/SkyCommon.hlsli" // EvaluateSky

uint64_t GeoTableAddress()
{
	return (uint64_t(ReflGeoTableAddrHi) << 32) | uint64_t(ReflGeoTableAddrLo);
}

// -------- RNG: PCG hash, deterministic per (pixel, global sample index) so a converged frame is reproducible.
uint PcgHash(uint v)
{
	uint state = v * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}
struct Rng
{
	uint s;
};
Rng SeedRng(uint2 px, uint sampleIdx)
{
	Rng r;
	r.s = PcgHash(px.x * 1973u + px.y * 9277u + sampleIdx * 26699u + FrameCounter * 60493u + 1u);
	return r;
}
float NextFloat(inout Rng r)
{
	r.s = r.s * 747796405u + 2891336453u;
	uint word = ((r.s >> ((r.s >> 28u) + 4u)) ^ r.s) * 277803737u;
	return float((word >> 22u) ^ word) * (1.0 / 4294967296.0);
}

// -------- Orthonormal basis (Duff et al. 2017).
void OnbFromNormal(float3 n, out float3 t, out float3 b)
{
	const float sgn = n.z >= 0.0 ? 1.0 : -1.0;
	const float a = -1.0 / (sgn + n.z);
	const float bb = n.x * n.y * a;
	t = float3(1.0 + sgn * n.x * n.x * a, sgn * bb, -sgn * n.x);
	b = float3(bb, sgn + n.y * n.y * a, -n.y);
}
float3 ToWorld(float3 v, float3 n)
{
	float3 t, b;
	OnbFromNormal(n, t, b);
	return v.x * t + v.y * b + v.z * n;
}

// Robust ray-origin offset (Wächter & Binder, "A Fast and Robust Method for Avoiding Self-Intersection",
// Ray Tracing Gems ch.6). Scales the offset with the surface's world-coordinate magnitude via integer ULP
// arithmetic, so it self-adapts to scene scale instead of a fixed epsilon (which is too small on large
// scenes like Sponza -> bounce/shadow rays re-hit the surface they left -> fixed bright/dark specks). This is
// THE production fix for self-intersection on scaled/instanced geometry; it needs no traversal changes.
float3 OffsetRay(float3 p, float3 n)
{
	const float intScale = 256.0;
	const float floatScale = 1.0 / 65536.0;
	const float origin = 1.0 / 32.0;
	const int3 ofI = int3(intScale * n);
	const float3 pI = asfloat(asint(p) + int3(p.x < 0.0 ? -ofI.x : ofI.x,
	                                          p.y < 0.0 ? -ofI.y : ofI.y,
	                                          p.z < 0.0 ? -ofI.z : ofI.z));
	return float3(abs(p.x) < origin ? p.x + floatScale * n.x : pI.x,
	              abs(p.y) < origin ? p.y + floatScale * n.y : pI.y,
	              abs(p.z) < origin ? p.z + floatScale * n.z : pI.z);
}

// Uniformly sample a direction inside the cone of half-angle acos(cosThetaMax) around `axis`. Used to give
// the sun a FINITE angular size for NEE (a true delta would make its specular reflection a single infinite
// hot pixel on smooth surfaces — the "bright stuck dot"). cosThetaMax == 1 collapses back to the exact axis.
float3 SampleCone(float3 axis, float cosThetaMax, float u1, float u2)
{
	const float cosTheta = 1.0 - u1 * (1.0 - cosThetaMax);
	const float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
	const float phi = 2.0 * PI * u2;
	const float3 local = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
	return normalize(ToWorld(local, axis));
}

// -------- Microfacet terms (metallic-roughness, GGX / Smith / Schlick).
float D_GGX(float NoH, float a)
{
	const float a2 = a * a;
	const float d = (NoH * NoH) * (a2 - 1.0) + 1.0;
	return a2 / max(PI * d * d, 1e-8);
}
float V_SmithGGX(float NoV, float NoL, float a)
{
	// Height-correlated Smith visibility (Heitz 2014), a = alpha = roughness^2. Returns V = G / (4 NoV NoL),
	// i.e. it already folds in the BRDF's 1/(4 NoV NoL) denominator, so specular = D * V * F. Exact (not the
	// Schlick-GGX approximation), the production-standard geometry term.
	const float a2 = a * a;
	const float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
	const float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
	return 0.5 / max(gv + gl, 1e-6);
}

// Estevez et al. 2019 (Ray Tracing Gems ch.12) shadow-terminator softening. The interpolated/normal-mapped
// shading normal Ns diverges from the flat geometric normal Ng; near the terminator that produces the abrupt
// black facet fringe. This returns 1 where they agree and falls smoothly to 0 as the light grazes the
// GEOMETRIC surface, guessing a microfacet roughness from the divergence and applying a Smith G1. Applied to
// the direct (NEE) contributions.
float TerminatorG(float3 Ng, float3 Ns, float3 L)
{
	const float cosi = dot(Ns, L);
	if (cosi <= 0.0)
	{
		return 0.0;
	}
	const float cosd = min(abs(dot(Ng, Ns)), 1.0);
	const float cosd2 = cosd * cosd;
	const float tan2d = (1.0 - cosd2) / max(cosd2, 1e-6);
	const float alpha2 = saturate(0.125 * tan2d);
	const float cosi2 = cosi * cosi;
	const float tan2i = (1.0 - cosi2) / max(cosi2, 1e-6);
	return 2.0 / (1.0 + sqrt(1.0 + alpha2 * tan2i));
}
float3 F_Schlick(float VoH, float3 f0)
{
	return f0 + (float3(1.0, 1.0, 1.0) - f0) * pow(saturate(1.0 - VoH), 5.0);
}

struct Hit
{
	bool valid;
	float3 pos;
	float3 Ng;      // geometric normal (world)
	float3 N;       // shading normal (normal-mapped if a map exists)
	float3 albedo;  // base color * albedo tex
	float metallic;
	float roughness; // perceptual, clamped
	float3 emissive;
};

// Closest-hit trace with cutout alpha (masked instances are FORCE_NON_OPAQUE -> surfaced as candidates).
bool TraceClosest(float3 origin, float3 dir, float tMax, uint64_t tableAddr, out uint instId, out uint prim, out float2 bary, out float t)
{
	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = dir;
	ray.TMin = 1e-4;
	ray.TMax = tMax;
	RayQuery<RAY_FLAG_NONE> q;
	q.TraceRayInline(SceneTLAS, RAY_FLAG_NONE, 0xFF, ray);
	while (q.Proceed())
	{
		if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
		    RTCommitCandidate(tableAddr, q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics(), LinearSampler))
		{
			q.CommitNonOpaqueTriangleHit();
		}
	}
	if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
	{
		return false;
	}
	instId = q.CommittedInstanceID();
	prim = q.CommittedPrimitiveIndex();
	bary = q.CommittedTriangleBarycentrics();
	t = q.CommittedRayT();
	return true;
}

float RTShadow(float3 pos, float3 Ng, float3 L, float tMax, uint64_t tableAddr)
{
	uint i;
	uint p;
	float2 b;
	float tt;
	// Scale-aware offset off the surface (see OffsetRay) so the shadow ray doesn't self-intersect on large scenes.
	const float3 o = OffsetRay(pos, Ng);
	// Occluded if anything lies between the surface and the light (tMax = light distance; 1e30 for the sun).
	return TraceClosest(o, L, tMax, tableAddr, i, p, b, tt) ? (1.0 - ShadowStrength) : 1.0;
}

// Object-space vertex POSITION (float3 @ offset 0 in the 48-byte Vertex) by device address (#153): used to
// derive the TRUE flat triangle normal for the ray offset + shadow-terminator handling.
float3 LoadVertexPos(uint64_t vertexAddr, uint index)
{
	const uint64_t a = vertexAddr + uint64_t(index) * 48ull;
	return float3(vk::RawBufferLoad<float>(a, 4), vk::RawBufferLoad<float>(a + 4, 4), vk::RawBufferLoad<float>(a + 8, 4));
}

// Object-space vertex TANGENT (float4 @ offset 32 in the 48-byte Vertex: xyz direction, w bitangent handedness,
// the glTF/assimp convention). Used to build the TBN for normal mapping so the reference has the same bump
// detail as the real-time forward path (matches Mesh.vert's o.TangentWS).
float4 LoadVertexTangent(uint64_t vertexAddr, uint index)
{
	const uint64_t a = vertexAddr + uint64_t(index) * 48ull + 32ull;
	return float4(vk::RawBufferLoad<float>(a, 4), vk::RawBufferLoad<float>(a + 4, 4), vk::RawBufferLoad<float>(a + 8, 4), vk::RawBufferLoad<float>(a + 12, 4));
}

// Resolve a committed hit to the full PBR surface via the geometry table (#153 PBR block).
Hit ResolvePbrHit(uint64_t tableAddr, uint instId, uint prim, float2 bary, float3 rayDir, float3 hitPos)
{
	Hit h;
	h.valid = true;
	h.pos = hitPos;

	const GeoRecord rec = LoadGeoRecord(tableAddr, instId);
	const uint64_t idxBase = rec.IndexAddress + uint64_t(prim) * 12ull;
	const uint i0 = vk::RawBufferLoad<uint>(idxBase + 0, 4);
	const uint i1 = vk::RawBufferLoad<uint>(idxBase + 4, 4);
	const uint i2 = vk::RawBufferLoad<uint>(idxBase + 8, 4);
	const float w = 1.0 - bary.x - bary.y;
	const float2 uv = w * LoadVertexUV(rec.VertexAddress, i0) + bary.x * LoadVertexUV(rec.VertexAddress, i1) + bary.y * LoadVertexUV(rec.VertexAddress, i2);

	// Shading normal = interpolated vertex normals (smooth), optionally perturbed by the normal map. Geometric
	// normal = the TRUE flat triangle normal (cross of the two edges). Using the FLAT normal for the ray offset
	// avoids the self-shadow "black aliasing" the interpolated normal causes on faceted geometry; the residual
	// grazing terminator is softened by TerminatorG (Estevez) at the NEE sites.
	const float3 nObj = w * LoadVertexNormal(rec.VertexAddress, i0) + bary.x * LoadVertexNormal(rec.VertexAddress, i1) + bary.y * LoadVertexNormal(rec.VertexAddress, i2);
	const float3 e0 = LoadVertexPos(rec.VertexAddress, i1) - LoadVertexPos(rec.VertexAddress, i0);
	const float3 e1 = LoadVertexPos(rec.VertexAddress, i2) - LoadVertexPos(rec.VertexAddress, i0);
	float3 Ng = normalize(mul(cross(e0, e1), (float3x3)rec.Model)); // flat geometric normal (world)
	float3 Ns = normalize(mul(nObj, (float3x3)rec.Model));          // smooth shading normal (world)
	// Face the geometric normal toward the incoming ray, then keep the shading normal in that same hemisphere.
	if (dot(Ng, rayDir) > 0.0)
	{
		Ng = -Ng;
	}
	if (dot(Ns, Ng) < 0.0)
	{
		Ns = -Ns;
	}

	// Normal mapping: perturb Ns by the tangent-space normal map, matching the real-time forward's ResolveNormal
	// so the reference carries the same bump detail for the A/B comparison. Build the TBN from the interpolated
	// vertex tangent (Gram-Schmidt vs Ns), and keep the mapped normal in the geometric hemisphere (a too-extreme
	// map that flips below the surface is discarded for this hit rather than producing a black self-shadow).
	if (rec.NormalTextureIndex != 0)
	{
		const float4 tObj = w * LoadVertexTangent(rec.VertexAddress, i0) + bary.x * LoadVertexTangent(rec.VertexAddress, i1) + bary.y * LoadVertexTangent(rec.VertexAddress, i2);
		float3 T = normalize(mul(tObj.xyz, (float3x3)rec.Model));
		T = normalize(T - Ns * dot(Ns, T)); // orthonormalize against the shading normal
		if (dot(T, T) > 0.0)
		{
			const float3 B = cross(Ns, T) * (tObj.w < 0.0 ? -1.0 : 1.0);
			const float3 s = Textures[NonUniformResourceIndex(rec.NormalTextureIndex)].SampleLevel(LinearSampler, uv, 0).xyz * 2.0 - 1.0;
			const float3 Nm = normalize(s.x * T + s.y * B + s.z * Ns);
			if (dot(Nm, Ng) > 0.0)
			{
				Ns = Nm;
			}
		}
	}

	// Robust shading-normal adaptation. A grazing interpolated/normal-mapped shading normal can tilt past the
	// silhouette so it faces AWAY from the viewer (dot(Ns, V) <= 0), which makes the microfacet BSDF undefined:
	// EvalBsdf/SampleBsdf then return black, producing a dark rim on every silhouette (foliage worst, since the
	// normal map pushes Ns furthest off the geometric normal). Bend Ns just into the view hemisphere so NoV > 0.
	// This is the load-bearing part of Cycles' ensure_valid_reflection / Schussler 2017 (which additionally keep
	// the specular reflection above the surface); it removes the rim. V = -rayDir (points back toward the camera
	// / previous path vertex).
	const float3 V = -rayDir;
	const float NoVs = dot(Ns, V);
	if (NoVs < 1e-3)
	{
		Ns = normalize(Ns + V * (1e-3 - NoVs)); // dot(Ns + V*(eps - NoVs), V) = eps > 0
	}

	h.Ng = Ng; // flat: ray offsets + shadow-ray origin + terminator reference
	h.N = Ns;  // shading normal (normal-mapped, view-hemisphere-adapted): BRDF shading

	float3 albedo = rec.BaseColor.rgb;
	if (rec.AlbedoTextureIndex != 0)
	{
		albedo *= Textures[NonUniformResourceIndex(rec.AlbedoTextureIndex)].SampleLevel(LinearSampler, uv, 0).rgb;
	}
	h.albedo = albedo;

	float metallic = rec.Metallic;
	float roughness = rec.Roughness;
	if (rec.MetallicRoughnessTextureIndex != 0)
	{
		const float3 mr = Textures[NonUniformResourceIndex(rec.MetallicRoughnessTextureIndex)].SampleLevel(LinearSampler, uv, 0).rgb;
		roughness *= mr.g; // glTF packing: roughness in G, metallic in B
		metallic *= mr.b;
	}
	h.metallic = saturate(metallic);
	h.roughness = clamp(roughness, 0.04, 1.0);

	float3 emissive = rec.Emissive;
	if (rec.EmissiveTextureIndex != 0)
	{
		emissive *= Textures[NonUniformResourceIndex(rec.EmissiveTextureIndex)].SampleLevel(LinearSampler, uv, 0).rgb;
	}
	h.emissive = emissive;
	return h;
}

// Evaluate the full BSDF (diffuse + specular) for a given L, returning brdf * NoL (radiance weight).
float3 EvalBsdf(Hit h, float3 V, float3 L)
{
	const float NoL = dot(h.N, L);
	const float NoV = dot(h.N, V);
	if (NoL <= 0.0 || NoV <= 0.0)
	{
		return float3(0.0, 0.0, 0.0);
	}
	const float3 H = normalize(V + L);
	const float NoH = saturate(dot(h.N, H));
	const float VoH = saturate(dot(V, H));

	const float3 f0 = lerp(float3(0.04, 0.04, 0.04), h.albedo, h.metallic);
	const float3 diffuse = h.albedo * (1.0 - h.metallic) * INV_PI;

	const float a = h.roughness * h.roughness;
	const float D = D_GGX(NoH, a);
	const float Vis = V_SmithGGX(saturate(NoV), saturate(NoL), a);
	const float3 F = F_Schlick(VoH, f0);
	const float3 spec = D * Vis * F; // height-correlated Smith V already includes the 1/(4 NoV NoL) denominator

	return (diffuse + spec) * NoL;
}

// Importance-sample the BSDF: pick diffuse or specular lobe, return the new direction and the throughput
// weight = brdf * NoL / pdf (already divided by the lobe-selection probability). Returns false to terminate.
bool SampleBsdf(Hit h, float3 V, inout Rng rng, out float3 L, out float3 weight)
{
	L = float3(0.0, 0.0, 0.0);
	weight = float3(0.0, 0.0, 0.0);
	const float NoV = dot(h.N, V);
	if (NoV <= 0.0)
	{
		return false;
	}

	const float3 f0 = lerp(float3(0.04, 0.04, 0.04), h.albedo, h.metallic);
	const float3 diffAlbedo = h.albedo * (1.0 - h.metallic);
	// Lobe-selection probability from luminance so the split matches each lobe's contribution.
	const float wDiff = dot(diffAlbedo, float3(0.2126, 0.7152, 0.0722));
	const float wSpec = dot(f0, float3(0.2126, 0.7152, 0.0722));
	float pSpec = wSpec / max(wDiff + wSpec, 1e-4);
	pSpec = clamp(pSpec, 0.1, 0.9); // keep both lobes sampled so neither is starved
	const float pDiff = 1.0 - pSpec;

	const float a = h.roughness * h.roughness;

	// Pick a lobe and sample a direction from it. The WEIGHT is computed once below from the mixture pdf, so
	// both lobes' pdfs count regardless of which lobe generated L (correct one-sample MIS).
	if (NextFloat(rng) < pDiff)
	{
		// Cosine-weighted diffuse hemisphere.
		const float u1 = NextFloat(rng);
		const float u2 = NextFloat(rng);
		const float r = sqrt(u1);
		const float phi = 2.0 * PI * u2;
		const float3 local = float3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u1)));
		L = normalize(ToWorld(local, h.N));
	}
	else
	{
		// GGX NDF sample of the half-vector, reflect V about it.
		const float u1 = NextFloat(rng);
		const float u2 = NextFloat(rng);
		const float phi = 2.0 * PI * u1;
		const float cosT = sqrt((1.0 - u2) / max(1.0 + (a * a - 1.0) * u2, 1e-8));
		const float sinT = sqrt(max(0.0, 1.0 - cosT * cosT));
		const float3 Hlocal = float3(sinT * cos(phi), sinT * sin(phi), cosT);
		const float3 H = normalize(ToWorld(Hlocal, h.N));
		L = reflect(-V, H);
	}

	const float NoL = dot(h.N, L);
	if (NoL <= 0.0)
	{
		return false;
	}

	// One-sample MIS (balance heuristic) across the two lobes: L may have come from either, so weight the FULL
	// BSDF by the MIXTURE pdf p(L) = pDiff * pdf_diffuse(L) + pSpec * pdf_specular(L). Dividing by only the
	// sampled lobe's pdf (the previous version) over-weighted and produced fireflies.
	const float3 H = normalize(V + L);
	const float NoH = saturate(dot(h.N, H));
	const float VoH = saturate(dot(V, H));
	const float pdfDiff = NoL * INV_PI;
	const float pdfSpec = (VoH > 0.0) ? (D_GGX(NoH, a) * NoH / (4.0 * VoH)) : 0.0;
	const float mixPdf = pDiff * pdfDiff + pSpec * pdfSpec;
	if (mixPdf <= 1e-8)
	{
		return false;
	}
	weight = EvalBsdf(h, V, L) / mixPdf;
	// Path regularization: bound the per-bounce throughput multiplier so a grazing/low-pdf near-mirror bounce
	// can't concentrate weight into a fixed hot pixel that never converges. 0 = off (unbiased). Preserves
	// reflection sharpness (unlike roughness regularization); small bias only on the rare exploding paths.
	if (MaxBounceWeight > 0.0)
	{
		weight = min(weight, float3(MaxBounceWeight, MaxBounceWeight, MaxBounceWeight));
	}
	return true;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= OutSize.x || id.y >= OutSize.y)
	{
		return;
	}

	const uint64_t tableAddr = GeoTableAddress();
	const uint bounces = max(MaxBounces, 1u);
	const uint spp = max(SamplesPerFrame, 1u);
	const float3 toSun = (LightCount > 0) ? normalize(-SunDirection) : float3(0, 0, 0);
	const float3 sunRad = SunColor * SunIntensity;

	float3 mean = (Reset != 0u) ? float3(0, 0, 0) : AccumOut[id.xy].rgb;
	uint globalIdx = BaseSampleCount;

	for (uint s = 0; s < spp; ++s)
	{
		Rng rng = SeedRng(id.xy, globalIdx);

		// Primary ray with per-sample sub-pixel jitter (box filter AA).
		const float2 jitter = float2(NextFloat(rng), NextFloat(rng));
		const float2 uv = (float2(id.xy) + jitter) / float2(OutSize);
		const float2 ndc = uv * 2.0 - 1.0;
		float4 farH = mul(float4(ndc, 1.0, 1.0), InvViewProj);
		const float3 farW = farH.xyz / farH.w;
		float3 origin = CameraPosition;
		float3 dir = normalize(farW - CameraPosition);

		float3 radiance = float3(0, 0, 0);
		float3 throughput = float3(1, 1, 1);

		for (uint bounce = 0; bounce < bounces; ++bounce)
		{
			uint instId; uint prim; float2 bary; float tHit;
			if (!TraceClosest(origin, dir, 1e30, tableAddr, instId, prim, bary, tHit))
			{
				// Miss: analytic sky WITHOUT the sun disk (the sun is NEE'd). This is the environment light.
				radiance += throughput * EvaluateSky(dir, SkyZenithColor, SkyHorizonColor, GroundColor, float3(0, 0, 0), float3(0, 0, 0));
				break;
			}

			const float3 hitPos = origin + dir * tHit;
			const Hit h = ResolvePbrHit(tableAddr, instId, prim, bary, dir, hitPos);
			const float3 V = -dir;

			// Emissive surface.
			radiance += throughput * h.emissive;

			// NEE to the sun as a FINITE disk light: sample a direction within its angular cone (uniform), so the
			// reflected sun on smooth surfaces is a converging soft highlight instead of a single infinite hot
			// pixel (the delta-light "bright stuck dot"). Unbiased: with uniform cone sampling the L_sun/solid-
			// angle and pdf cancel, so the per-sample estimator is EvalBsdf(jittered L) * irradiance — identical
			// to the delta result in expectation for diffuse, but soft (and converging) for specular. Also yields
			// soft sun shadows for free (the shadow ray follows the jittered L), matching the RT soft path.
			if (LightCount > 0)
			{
				const float su1 = NextFloat(rng);
				const float su2 = NextFloat(rng);
				const float3 L = SampleCone(toSun, SunCosThetaMax, su1, su2);
				const float ndl = dot(h.N, L);
				if (ndl > 0.0)
				{
					const float vis = RTShadow(h.pos, h.Ng, L, 1e30, tableAddr);
					radiance += throughput * EvalBsdf(h, V, L) * sunRad * vis * TerminatorG(h.Ng, h.N, L);
				}
			}

			// NEE to point lights: sample each, inverse-square + range falloff, shadow ray to the light.
			const uint pc = min(PointCount, MAX_PT_POINT_LIGHTS);
			for (uint pli = 0; pli < pc; ++pli)
			{
				const float4 p0 = PointLights[pli * 2u];
				const float4 p1 = PointLights[pli * 2u + 1u];
				const float3 lightPos = p0.xyz;
				const float range = p0.w;
				const float3 d = lightPos - h.pos;
				const float dist = length(d);
				if (dist < 1e-4 || (range > 0.0 && dist > range))
				{
					continue;
				}
				// Finite light size: sample within the cone the light's sphere subtends, so its reflection on
				// smooth surfaces is a converging soft highlight, not a delta hot pixel. radius 0 => delta.
				const float3 toL = d / dist;
				const float sinHalf = saturate(LightSourceRadius / dist);
				const float cosMax = sqrt(max(0.0, 1.0 - sinHalf * sinHalf));
				const float3 L = SampleCone(toL, cosMax, NextFloat(rng), NextFloat(rng));
				if (dot(h.N, L) <= 0.0)
				{
					continue;
				}
				const float vis = RTShadow(h.pos, h.Ng, L, dist, tableAddr);
				const float atten = PositionalAttenuation(dist, range);
				radiance += throughput * EvalBsdf(h, V, L) * p1.xyz * p1.w * atten * vis * TerminatorG(h.Ng, h.N, L);
			}

			// NEE to spot lights: point-light NEE plus the cone falloff (smoothstep between cosOuter/cosInner).
			const uint sc = min(SpotCount, MAX_PT_SPOT_LIGHTS);
			for (uint sli = 0; sli < sc; ++sli)
			{
				const float4 s0 = SpotLights[sli * 4u];
				const float4 s1 = SpotLights[sli * 4u + 1u];
				const float4 s2 = SpotLights[sli * 4u + 2u];
				const float cosOuter = SpotLights[sli * 4u + 3u].x;
				const float3 lightPos = s0.xyz;
				const float range = s0.w;
				const float3 spotDir = s2.xyz; // direction the spot points (world)
				const float cosInner = s2.w;
				const float3 d = lightPos - h.pos;
				const float dist = length(d);
				if (dist < 1e-4 || (range > 0.0 && dist > range))
				{
					continue;
				}
				const float3 toL = d / dist;
				// Spot cone falloff on the CENTER direction (stable); the BRDF/shadow use the finite-size sample.
				const float cd = dot(normalize(spotDir), -toL);
				const float cone = smoothstep(cosOuter, cosInner, cd);
				if (cone <= 0.0)
				{
					continue;
				}
				const float sinHalf = saturate(LightSourceRadius / dist);
				const float cosMax = sqrt(max(0.0, 1.0 - sinHalf * sinHalf));
				const float3 L = SampleCone(toL, cosMax, NextFloat(rng), NextFloat(rng));
				if (dot(h.N, L) <= 0.0)
				{
					continue;
				}
				const float vis = RTShadow(h.pos, h.Ng, L, dist, tableAddr);
				const float atten = PositionalAttenuation(dist, range) * cone;
				radiance += throughput * EvalBsdf(h, V, L) * s1.xyz * s1.w * atten * vis * TerminatorG(h.Ng, h.N, L);
			}

			// BSDF-sampled continuation (indirect: GI / reflections / AO all emerge here).
			float3 newDir;
			float3 weight;
			if (!SampleBsdf(h, V, rng, newDir, weight))
			{
				break;
			}
			throughput *= weight;

			// Russian roulette after a few bounces.
			if (bounce >= 3u)
			{
				const float p = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05, 1.0);
				if (NextFloat(rng) > p)
				{
					break;
				}
				throughput /= p;
			}

			origin = OffsetRay(h.pos, h.Ng); // scale-aware offset (no self-intersection on large scenes)
			dir = newDir;
		}

		// Progressive running mean so the buffer is directly displayable (no divide pass).
		radiance = max(radiance, float3(0.0, 0.0, 0.0)); // drop negatives from degenerate samples
		if (any(isnan(radiance)) || any(isinf(radiance)))
		{
			radiance = float3(0.0, 0.0, 0.0);
		}
		// Firefly clamp (render.pathtrace.clamp): cap each sample's radiance so a rare bright path can't leave a
		// slow-to-average dot. Trades a little bias in genuine highlights for far cleaner convergence; 0 = off.
		if (FireflyClamp > 0.0)
		{
			radiance = min(radiance, float3(FireflyClamp, FireflyClamp, FireflyClamp));
		}
		mean += (radiance - mean) / float(globalIdx + 1u);
		globalIdx += 1u;
	}

	AccumOut[id.xy] = float4(mean, 1.0);
}
