#pragma once

#include <cstdint>

// CPU mirror of Engine/Shaders/Include/PtSampler.hlsli: Owen-scrambled 2D Sobol, padded per decision (#156).
// The integer code must stay identical to the shader copy.
namespace Snowstorm::QmcSampler
{
	constexpr uint32_t ReverseBits(uint32_t x)
	{
		x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
		x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
		x = ((x >> 4) & 0x0f0f0f0fu) | ((x & 0x0f0f0f0fu) << 4);
		x = ((x >> 8) & 0x00ff00ffu) | ((x & 0x00ff00ffu) << 8);
		return (x >> 16) | (x << 16);
	}

	constexpr uint32_t Hash(uint32_t v)
	{
		const uint32_t state = v * 747796405u + 2891336453u;
		const uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
		return (word >> 22u) ^ word;
	}

	constexpr uint32_t HashCombine(uint32_t a, uint32_t b)
	{
		return Hash(a ^ (Hash(b) + 0x9e3779b9u));
	}

	constexpr uint32_t LaineKarras(uint32_t x, uint32_t seed)
	{
		x += seed;
		x ^= x * 0x6c50b47cu;
		x ^= x * 0xb82f1e52u;
		x ^= x * 0xc7afe638u;
		x ^= x * 0x8d22f6e6u;
		return x;
	}

	// Owen scramble of the bit-reversed value: bit k of the input only influences bits above it in the reversed
	// domain, so a prefix of 2^k consecutive indices stays a permutation of itself (the net property survives).
	constexpr uint32_t NestedUniformScramble(uint32_t x, uint32_t seed)
	{
		return ReverseBits(LaineKarras(ReverseBits(x), seed));
	}

	struct Point2
	{
		uint32_t X;
		uint32_t Y;
	};

	constexpr Point2 Sobol2D(uint32_t index)
	{
		Point2 r{0, 0};
		uint32_t v1 = 1u << 31;
		uint32_t v2 = 1u << 31;
		for (; index != 0u; index >>= 1)
		{
			if (index & 1u)
			{
				r.X ^= v1;
				r.Y ^= v2;
			}
			v1 >>= 1;
			v2 ^= v2 >> 1;
		}
		return r;
	}

	// 24 bits keep the result exactly representable in a float and strictly below 1.
	constexpr float ToUnit(uint32_t x)
	{
		return static_cast<float>(x >> 8) * (1.0f / 16777216.0f);
	}

	struct Float2
	{
		float X;
		float Y;
	};

	constexpr Float2 Sample2D(uint32_t index, uint32_t pairId, uint32_t pixelSeed)
	{
		const uint32_t seed = HashCombine(pixelSeed, pairId);
		const uint32_t i = NestedUniformScramble(index, seed);
		const Point2 b = Sobol2D(i);
		return {ToUnit(NestedUniformScramble(b.X, HashCombine(seed, 1u))), ToUnit(NestedUniformScramble(b.Y, HashCombine(seed, 2u)))};
	}
}
