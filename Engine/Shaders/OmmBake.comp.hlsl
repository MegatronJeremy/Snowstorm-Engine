// OmmBake.comp — GPU opacity-micromap bake (#OMM B2). Produces the packed 4-state (2 bits/microtriangle)
// states buffer that VulkanMicromap builds a VkMicromapEXT from, by sampling a masked mesh's albedo alpha.
//
// Forward-only, grid-accumulation design: NVIDIA's index->barycentric inverse is proprietary, but the
// barycentric->index forward map (BarycentricsToSpaceFillingCurveIndex, the "bird curve", Werness — Khronos
// Vulkan spec VK_KHR/EXT_opacity_micromap) is open and is exactly the mapping the hardware reads with. So we
// walk a fine barycentric sample grid, forward-map each sample to its microtriangle, and OR an
// OPAQUE/TRANSPARENT bit into a per-microtriangle accumulator; a second pass classifies. Conservative: a
// microtriangle with mixed or no samples is UNKNOWN, which falls back to the exact any-hit test — so a wrong
// bake can only lose the perf win, never the image.
//
// Two passes over ONE pipeline, selected by Bake.Pass (0 = accumulate, 1 = classify+pack) with a barrier
// between them (C++ side): accumulate dispatches over triangle x sample-grid; pack over microtriangles.

struct BakeConstants
{
	uint VertexAddrLo; // mesh vertex buffer device address (stride 48, UV at +24)
	uint VertexAddrHi;
	uint IndexAddrLo; // mesh index buffer (uint32)
	uint IndexAddrHi;

	uint TriangleCount;
	uint SubdivisionLevel;   // 4^level microtriangles per triangle
	uint AlbedoTextureIndex; // bindless index into Textures[]; 0 = untextured (BaseColorAlpha only)
	float AlphaCutoff;

	float BaseColorAlpha; // material BaseColor.a (multiplies the sampled alpha)
	uint SamplesPerEdge;  // barycentric grid resolution per triangle edge (accumulate pass)
	uint Pass;            // 0 = accumulate, 1 = classify + pack
	uint _Pad;
};
[[vk::push_constant]] BakeConstants Bake;

// ---- Set 0 ----
// One uint per microtriangle: bit0 = saw OPAQUE sample, bit1 = saw TRANSPARENT sample. Accumulate ORs into
// it; pack reads it. Sized TriangleCount * 4^level by the caller, zero-initialised.
RWStructuredBuffer<uint> Accumulator : register(u1, space0);
// Packed 2-bit states, the micromap build input. Sized TriangleCount * BytesPerTriangle(level), zero-init.
RWByteAddressBuffer States : register(u2, space0);
SamplerState LinearSampler : register(s3, space0);

// ---- Set 3: engine bindless pool (gap-filled by the compute pipeline builder) ----
Texture2D Textures[] : register(t0, space3);

uint64_t VertexAddress() { return (uint64_t(Bake.VertexAddrHi) << 32) | uint64_t(Bake.VertexAddrLo); }
uint64_t IndexAddress() { return (uint64_t(Bake.IndexAddrHi) << 32) | uint64_t(Bake.IndexAddrLo); }
uint MicroTriCount() { return 1u << (2u * Bake.SubdivisionLevel); } // 4^level

// Mesh vertex TexCoord (float2 @ offset 24 in the 48-byte Vertex).
float2 LoadUV(uint64_t vtxAddr, uint index)
{
	const uint64_t a = vtxAddr + uint64_t(index) * 48ull + 24ull;
	return float2(vk::RawBufferLoad<float>(a, 4), vk::RawBufferLoad<float>(a + 4, 4));
}

// Barycentrics -> microtriangle linear index (bird-curve space-filling curve). Open reference from the
// Khronos Vulkan spec (VK_KHR/EXT_opacity_micromap, Werness 2022); the exact map the hardware uses.
uint BarycentricsToSpaceFillingCurveIndex(float u, float v, uint level)
{
	u = clamp(u, 0.0, 1.0);
	v = clamp(v, 0.0, 1.0);

	const float fu = u * float(1u << level);
	const float fv = v * float(1u << level);

	uint iu = uint(fu);
	uint iv = uint(fv);
	const float uf = fu - float(iu);
	const float vf = fv - float(iv);

	if (iu >= (1u << level)) iu = (1u << level) - 1u;
	if (iv >= (1u << level)) iv = (1u << level) - 1u;

	const uint iuv = iu + iv;
	if (iuv >= (1u << level)) iu -= iuv - (1u << level) + 1u;

	uint iw = ~(iu + iv);
	if (uf + vf >= 1.0 && iuv < (1u << level) - 1u) iw--;

	uint b0 = ~(iu ^ iw);
	b0 &= (1u << level) - 1u;
	const uint t = (iu ^ iv) & b0;

	uint f = t;
	f ^= f >> 1u;
	f ^= f >> 2u;
	f ^= f >> 4u;
	f ^= f >> 8u;
	uint b1 = ((f ^ iu) & ~b0) | t;

	b0 = (b0 | (b0 << 8u)) & 0x00ff00ffu;
	b0 = (b0 | (b0 << 4u)) & 0x0f0f0f0fu;
	b0 = (b0 | (b0 << 2u)) & 0x33333333u;
	b0 = (b0 | (b0 << 1u)) & 0x55555555u;

	b1 = (b1 | (b1 << 8u)) & 0x00ff00ffu;
	b1 = (b1 | (b1 << 4u)) & 0x0f0f0f0fu;
	b1 = (b1 | (b1 << 2u)) & 0x33333333u;
	b1 = (b1 | (b1 << 1u)) & 0x55555555u;

	return b0 | (b1 << 1u);
}

// Sampled alpha at a triangle-local barycentric (u,v): interpolate the 3 vertex UVs, sample albedo alpha.
float SampleAlpha(uint64_t vtxAddr, uint i0, uint i1, uint i2, float u, float v)
{
	const float w = 1.0 - u - v;
	const float2 uv = w * LoadUV(vtxAddr, i0) + u * LoadUV(vtxAddr, i1) + v * LoadUV(vtxAddr, i2);
	float a = Bake.BaseColorAlpha;
	if (Bake.AlbedoTextureIndex != 0)
	{
		a *= Textures[NonUniformResourceIndex(Bake.AlbedoTextureIndex)].SampleLevel(LinearSampler, uv, 0).a;
	}
	return a;
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (Bake.Pass == 0)
	{
		// Accumulate: one thread per (triangle, sample). The sample grid is a SamplesPerEdge x SamplesPerEdge
		// lower-left barycentric lattice (u+v <= 1). Flattened: thread = triangle * samplesPerTri + sampleIdx.
		const uint samplesPerTri = Bake.SamplesPerEdge * Bake.SamplesPerEdge;
		if (id.x >= Bake.TriangleCount * samplesPerTri) return;

		const uint tri = id.x / samplesPerTri;
		const uint s = id.x % samplesPerTri;
		const uint su = s % Bake.SamplesPerEdge;
		const uint sv = s / Bake.SamplesPerEdge;

		// Sample at cell centers; skip the upper (u+v>1) half of the square lattice.
		const float u = (float(su) + 0.5) / float(Bake.SamplesPerEdge);
		const float v = (float(sv) + 0.5) / float(Bake.SamplesPerEdge);
		if (u + v > 1.0) return;

		const uint64_t iBase = IndexAddress() + uint64_t(tri) * 12ull;
		const uint i0 = vk::RawBufferLoad<uint>(iBase + 0, 4);
		const uint i1 = vk::RawBufferLoad<uint>(iBase + 4, 4);
		const uint i2 = vk::RawBufferLoad<uint>(iBase + 8, 4);

		const float alpha = SampleAlpha(VertexAddress(), i0, i1, i2, u, v);
		const uint microTri = BarycentricsToSpaceFillingCurveIndex(u, v, Bake.SubdivisionLevel);
		const uint globalMicro = tri * MicroTriCount() + microTri;

		const uint bit = (alpha >= Bake.AlphaCutoff) ? 1u : 2u; // bit0 = opaque, bit1 = transparent
		uint prev;
		InterlockedOr(Accumulator[globalMicro], bit, prev);
	}
	else
	{
		// Classify + pack: one thread per microtriangle. 4-state values: 0 = TRANSPARENT, 1 = OPAQUE,
		// 2 = UNKNOWN_TRANSPARENT, 3 = UNKNOWN_OPAQUE. Mixed or unsampled -> UNKNOWN_OPAQUE (conservative: the
		// any-hit test resolves it exactly). Packed 2 bits/microtriangle into the States byte-address buffer.
		if (id.x >= Bake.TriangleCount * MicroTriCount()) return;

		const uint acc = Accumulator[id.x];
		uint state;
		if (acc == 1u) state = 1u;      // opaque only
		else if (acc == 2u) state = 0u; // transparent only
		else state = 3u;                // mixed (3) or unsampled (0) -> UNKNOWN_OPAQUE

		// Pack the 2-bit state at bit offset id.x*2. Each 32-bit word holds 16 microtriangles; concurrent
		// threads share a word, so OR atomically into the zero-initialised buffer.
		const uint wordByte = (id.x >> 4u) * 4u;
		const uint bitInWord = (id.x & 15u) * 2u;
		uint prev;
		States.InterlockedOr(wordByte, state << bitInWord, prev);
	}
}
