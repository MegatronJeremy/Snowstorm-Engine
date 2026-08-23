// RTGeometry.hlsli — the per-instance geometry-table record + its attribute reads, plus the inline-RayQuery
// any-hit ALPHA TEST shared by every RT pass (shadows/AO/GI/reflection). Split out of RTHitShading.hlsli so
// the occupancy-only passes (shadows, AO) can alpha-test cutout (glTF MASK) geometry WITHOUT pulling in the
// heavy sun/IBL hit-SHADING contract that RTHitShading's ShadeSurfaceHit needs.
//
// CONTRACT — before #include-ing this file the includer MUST declare the bindless albedo pool:
//   Texture2D Textures[] : register(t0, space3);
// DefaultLit gets it from Engine.hlsli; the compute passes declare it themselves (RTHitShading declares it
// for GI/Reflection). The alpha-test entry point takes its SamplerState as a parameter, so the includer only
// needs some wrapping/linear sampler to pass in — no fixed sampler name is assumed here.
//
// Record layout MUST match GeometryRecord (ReflectionGeometrySingleton.hpp) byte-for-byte (144-byte stride);
// the offsets below are the contract (see that header). This is the ONE place the layout is decoded. The PBR
// block ([112]..) is appended after Model (#153) so existing readers that stop at Model are byte-unaffected.

#ifndef SNOWSTORM_RT_GEOMETRY_HLSLI
#define SNOWSTORM_RT_GEOMETRY_HLSLI

struct GeoRecord
{
	uint64_t VertexAddress;   // [0]  mesh vertex buffer (stride 48)
	uint64_t IndexAddress;    // [8]  mesh index buffer (uint32)
	uint AlbedoTextureIndex;  // [16] bindless index into Textures[] (0 = none)
	uint AlphaMaskEnabled;    // [20] 1 = alpha-cutout (glTF MASK)
	float AlphaCutoff;        // [24] albedo.a threshold when masked
	float4 BaseColor;         // [32] base-color factor (its .a scales the mask alpha)
	float4x4 Model;           // [48] object->world
	// PBR block (#153), appended after Model. The path tracer reads all of it; the compute RT hit shading reads
	// Metallic (+ its map) to match the forward's diffuse kd. The visibility-only passes ignore the block.
	uint MetallicRoughnessTextureIndex; // [112] glTF MR (.b metallic, .g roughness)
	uint NormalTextureIndex;            // [116] tangent-space normal map
	uint EmissiveTextureIndex;          // [120] emissive map
	float Metallic;                     // [124]
	float Roughness;                    // [128]
	float3 Emissive;                    // [132] emissive color factor (3 floats)
};

GeoRecord LoadGeoRecord(uint64_t tableAddr, uint instanceIndex)
{
	const uint64_t base = tableAddr + uint64_t(instanceIndex) * 144ull;
	GeoRecord r;
	r.VertexAddress = vk::RawBufferLoad<uint64_t>(base + 0, 8);
	r.IndexAddress = vk::RawBufferLoad<uint64_t>(base + 8, 8);
	r.AlbedoTextureIndex = vk::RawBufferLoad<uint>(base + 16, 4);
	r.AlphaMaskEnabled = vk::RawBufferLoad<uint>(base + 20, 4);
	r.AlphaCutoff = vk::RawBufferLoad<float>(base + 24, 4);
	r.BaseColor = float4(vk::RawBufferLoad<float>(base + 32, 4), vk::RawBufferLoad<float>(base + 36, 4),
	                     vk::RawBufferLoad<float>(base + 40, 4), vk::RawBufferLoad<float>(base + 44, 4));
	// mat4 is 16 contiguous floats at offset 48 (column-major, matching glm).
	float4 c0 = float4(vk::RawBufferLoad<float>(base + 48, 4), vk::RawBufferLoad<float>(base + 52, 4),
	                   vk::RawBufferLoad<float>(base + 56, 4), vk::RawBufferLoad<float>(base + 60, 4));
	float4 c1 = float4(vk::RawBufferLoad<float>(base + 64, 4), vk::RawBufferLoad<float>(base + 68, 4),
	                   vk::RawBufferLoad<float>(base + 72, 4), vk::RawBufferLoad<float>(base + 76, 4));
	float4 c2 = float4(vk::RawBufferLoad<float>(base + 80, 4), vk::RawBufferLoad<float>(base + 84, 4),
	                   vk::RawBufferLoad<float>(base + 88, 4), vk::RawBufferLoad<float>(base + 92, 4));
	float4 c3 = float4(vk::RawBufferLoad<float>(base + 96, 4), vk::RawBufferLoad<float>(base + 100, 4),
	                   vk::RawBufferLoad<float>(base + 104, 4), vk::RawBufferLoad<float>(base + 108, 4));
	r.Model = float4x4(c0, c1, c2, c3);
	// PBR block (#153) at [112]. Cheap trailing loads; unused by the non-PT readers (dead-code-eliminated there).
	r.MetallicRoughnessTextureIndex = vk::RawBufferLoad<uint>(base + 112, 4);
	r.NormalTextureIndex = vk::RawBufferLoad<uint>(base + 116, 4);
	r.EmissiveTextureIndex = vk::RawBufferLoad<uint>(base + 120, 4);
	r.Metallic = vk::RawBufferLoad<float>(base + 124, 4);
	r.Roughness = vk::RawBufferLoad<float>(base + 128, 4);
	r.Emissive = float3(vk::RawBufferLoad<float>(base + 132, 4), vk::RawBufferLoad<float>(base + 136, 4),
	                    vk::RawBufferLoad<float>(base + 140, 4));
	return r;
}

// Read a mesh vertex's TexCoord (float2 @ offset 24 in the 48-byte Vertex) by device address.
float2 LoadVertexUV(uint64_t vertexAddr, uint index)
{
	const uint64_t a = vertexAddr + uint64_t(index) * 48ull + 24ull;
	return float2(vk::RawBufferLoad<float>(a, 4), vk::RawBufferLoad<float>(a + 4, 4));
}

// Read a mesh vertex's object-space Normal (float3 @ offset 12) by device address.
float3 LoadVertexNormal(uint64_t vertexAddr, uint index)
{
	const uint64_t a = vertexAddr + uint64_t(index) * 48ull + 12ull;
	return float3(vk::RawBufferLoad<float>(a, 4), vk::RawBufferLoad<float>(a + 4, 4), vk::RawBufferLoad<float>(a + 8, 4));
}

// Interpolated albedo alpha at a hit triangle (record already loaded), for the cutout any-hit test.
float HitAlpha(GeoRecord rec, uint prim, float2 bary, SamplerState samp)
{
	const uint64_t idxBase = rec.IndexAddress + uint64_t(prim) * 12ull; // 3 * uint32
	const uint i0 = vk::RawBufferLoad<uint>(idxBase + 0, 4);
	const uint i1 = vk::RawBufferLoad<uint>(idxBase + 4, 4);
	const uint i2 = vk::RawBufferLoad<uint>(idxBase + 8, 4);
	const float w = 1.0 - bary.x - bary.y;
	const float2 uv = w * LoadVertexUV(rec.VertexAddress, i0) + bary.x * LoadVertexUV(rec.VertexAddress, i1) + bary.y * LoadVertexUV(rec.VertexAddress, i2);
	float a = rec.BaseColor.a;
	if (rec.AlbedoTextureIndex != 0)
	{
		a *= Textures[NonUniformResourceIndex(rec.AlbedoTextureIndex)].SampleLevel(samp, uv, 0).a;
	}
	return a;
}

// Any-hit decision for a CANDIDATE_NON_OPAQUE_TRIANGLE surfaced during RayQuery::Proceed(): return true to
// commit the hit (occlude), false to ignore it (the ray sees through this texel). A masked instance sees
// through texels whose alpha is below its cutoff; a non-masked non-opaque instance (shouldn't occur — only
// masked instances are FORCE_NON_OPAQUE) commits. tableAddr == 0 (table not published yet) commits, so a
// momentarily-missing table falls back to solid rather than dropping an occluder.
bool RTCommitCandidate(uint64_t tableAddr, uint instanceId, uint prim, float2 bary, SamplerState samp)
{
	if (tableAddr == 0)
	{
		return true;
	}
	const GeoRecord rec = LoadGeoRecord(tableAddr, instanceId);
	if (rec.AlphaMaskEnabled == 0)
	{
		return true;
	}
	return HitAlpha(rec, prim, bary, samp) >= rec.AlphaCutoff;
}

#endif // SNOWSTORM_RT_GEOMETRY_HLSLI
