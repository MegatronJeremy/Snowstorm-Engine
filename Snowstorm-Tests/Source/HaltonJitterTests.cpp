#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Snowstorm/Math/HaltonJitter.hpp"

#include <cstdint>

using namespace Snowstorm;
using Catch::Approx;

// The radical inverse is the mathematical core; pin the first few known values of each base so a wrong
// digit reversal (the classic Halton bug) is caught headlessly. Base-2: 1/2, 1/4, 3/4, 1/8, 5/8, ...;
// base-3: 1/3, 2/3, 1/9, 4/9, ...
TEST_CASE("Halton radical inverse matches known base-2 / base-3 values", "[math][jitter]")
{
	CHECK(HaltonRadicalInverse(1, 2) == Approx(0.5f));
	CHECK(HaltonRadicalInverse(2, 2) == Approx(0.25f));
	CHECK(HaltonRadicalInverse(3, 2) == Approx(0.75f));
	CHECK(HaltonRadicalInverse(4, 2) == Approx(0.125f));
	CHECK(HaltonRadicalInverse(5, 2) == Approx(0.625f));

	CHECK(HaltonRadicalInverse(1, 3) == Approx(1.0f / 3.0f));
	CHECK(HaltonRadicalInverse(2, 3) == Approx(2.0f / 3.0f));
	CHECK(HaltonRadicalInverse(3, 3) == Approx(1.0f / 9.0f));
	CHECK(HaltonRadicalInverse(4, 3) == Approx(4.0f / 9.0f));

	CHECK(HaltonRadicalInverse(0, 2) == Approx(0.0f)); // index 0 is the degenerate point
}

// The jitter offset feeds a sub-pixel projection shift; it MUST stay within +/-0.5px or it would shift the
// image by whole pixels (visible jumping, not the intended sub-pixel shimmer). Check the whole ring.
TEST_CASE("Halton jitter stays within +/- half a pixel", "[math][jitter]")
{
	constexpr uint32_t sampleCount = 8;
	for (uint64_t f = 0; f < 64; ++f)
	{
		const glm::vec2 j = HaltonJitterPixels(f, sampleCount);
		CHECK(j.x >= -0.5f);
		CHECK(j.x <= 0.5f);
		CHECK(j.y >= -0.5f);
		CHECK(j.y <= 0.5f);
	}
}

// Deterministic + periodic: same frame -> same offset (no hidden state), and the sequence repeats every
// sampleCount frames (the bounded ring the temporal resolve relies on).
TEST_CASE("Halton jitter is deterministic and cycles with sampleCount", "[math][jitter]")
{
	constexpr uint32_t sampleCount = 8;
	for (uint64_t f = 0; f < 32; ++f)
	{
		const glm::vec2 a = HaltonJitterPixels(f, sampleCount);
		const glm::vec2 b = HaltonJitterPixels(f, sampleCount);
		CHECK(a.x == Approx(b.x));
		CHECK(a.y == Approx(b.y));

		// Same phase one full ring later.
		const glm::vec2 next = HaltonJitterPixels(f + sampleCount, sampleCount);
		CHECK(a.x == Approx(next.x));
		CHECK(a.y == Approx(next.y));
	}
}

// The first sample must not be the degenerate (0,0): frameCounter 0 -> index 1, a real offset. A zero
// first frame would leave the very first jittered frame identical to unjittered (a subtle "jitter isn't
// working on frame 0" bug).
TEST_CASE("Halton jitter frame 0 is non-zero", "[math][jitter]")
{
	const glm::vec2 j = HaltonJitterPixels(0, 8);
	CHECK((j.x != Approx(0.0f) || j.y != Approx(0.0f)));
}
