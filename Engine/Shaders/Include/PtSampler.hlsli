#ifndef SS_PT_SAMPLER_HLSLI
#define SS_PT_SAMPLER_HLSLI

// Owen-scrambled 2D Sobol, padded per decision (Burley, "Practical Hash-based Owen Scrambling", JCGT 2020).
// Mirrored bit-for-bit by Render/QmcSampler.hpp, which the Catch2 tests exercise; change both together.

uint PtHash(uint v)
{
	uint state = v * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

uint PtHashCombine(uint a, uint b)
{
	return PtHash(a ^ (PtHash(b) + 0x9e3779b9u));
}

uint LaineKarras(uint x, uint seed)
{
	x += seed;
	x ^= x * 0x6c50b47cu;
	x ^= x * 0xb82f1e52u;
	x ^= x * 0xc7afe638u;
	x ^= x * 0x8d22f6e6u;
	return x;
}

uint NestedUniformScramble(uint x, uint seed)
{
	return reversebits(LaineKarras(reversebits(x), seed));
}

uint2 Sobol2D(uint index)
{
	uint2 r = uint2(0u, 0u);
	uint v1 = 1u << 31;
	uint v2 = 1u << 31;
	for (; index != 0u; index >>= 1)
	{
		if ((index & 1u) != 0u)
		{
			r.x ^= v1;
			r.y ^= v2;
		}
		v1 >>= 1;
		v2 ^= v2 >> 1;
	}
	return r;
}

float PtToUnit(uint x)
{
	return float(x >> 8) * (1.0 / 16777216.0);
}

float2 PtSample2D(uint index, uint pairId, uint pixelSeed)
{
	const uint seed = PtHashCombine(pixelSeed, pairId);
	const uint i = NestedUniformScramble(index, seed);
	const uint2 b = Sobol2D(i);
	return float2(PtToUnit(NestedUniformScramble(b.x, PtHashCombine(seed, 1u))),
	              PtToUnit(NestedUniformScramble(b.y, PtHashCombine(seed, 2u))));
}

#endif
