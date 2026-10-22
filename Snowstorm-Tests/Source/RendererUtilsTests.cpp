#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/RendererUtils.hpp"

#include <cmath>

using namespace Snowstorm;

TEST_CASE("ScaledExtent at scale 1 is the identity", "[render][scale]")
{
	for (const uint32_t dim : {1u, 2u, 3u, 719u, 1080u, 1919u, 4096u, 1u << 20})
		CHECK(ScaledExtent(dim, 1.0f) == dim);
}

TEST_CASE("ScaledExtent rounds half away from zero like std::lround", "[render][scale]")
{
	// Odd dimensions at 0.5 land exactly on .5: lround rounds up, where truncation would round down.
	CHECK(ScaledExtent(3, 0.5f) == 2);
	CHECK(ScaledExtent(5, 0.5f) == 3);
	CHECK(ScaledExtent(7, 0.5f) == 4);
	CHECK(ScaledExtent(1079, 0.5f) == 540);
	CHECK(ScaledExtent(1919, 0.5f) == 960);

	// Even dimensions are exact.
	CHECK(ScaledExtent(1080, 0.5f) == 540);
	CHECK(ScaledExtent(1920, 0.5f) == 960);

	// Off-half cases round to nearest.
	CHECK(ScaledExtent(10, 0.34f) == 3);
	CHECK(ScaledExtent(10, 0.36f) == 4);

	for (const uint32_t dim : {1u, 2u, 3u, 99u, 1000u, 1917u})
		CHECK(ScaledExtent(dim, 0.75f) == static_cast<uint32_t>(std::lround(static_cast<float>(dim) * 0.75f)));
}

TEST_CASE("ScaledExtent never returns zero", "[render][scale]")
{
	CHECK(ScaledExtent(0, 1.0f) == 1);
	CHECK(ScaledExtent(0, 2.0f) == 1);
	CHECK(ScaledExtent(1, 0.5f) == 1);
	CHECK(ScaledExtent(1, 0.1f) == 1);
	CHECK(ScaledExtent(100, 0.001f) == 1);
	CHECK(ScaledExtent(1920, 0.0f) == 1);
	CHECK(ScaledExtent(0, 0.0f) == 1);
}

TEST_CASE("ScaledExtent upscales and does not overflow at scale 2", "[render][scale]")
{
	CHECK(ScaledExtent(1, 2.0f) == 2);
	CHECK(ScaledExtent(1920, 2.0f) == 3840);
	CHECK(ScaledExtent(16384, 2.0f) == 32768);
	CHECK(ScaledExtent(1u << 20, 2.0f) == (1u << 21));
	CHECK(ScaledExtent(1u << 24, 2.0f) == (1u << 25));
}

TEST_CASE("ScaledExtent is monotonic in the scale", "[render][scale]")
{
	uint32_t prev = 0;
	for (float s = 0.05f; s <= 2.0f; s += 0.05f)
	{
		const uint32_t e = ScaledExtent(1357, s);
		CHECK(e >= prev);
		prev = e;
	}
}

TEST_CASE("Render target format constants", "[render][format]")
{
	// HDR scene colour and velocity must stay float so highlights above 1.0 and negative velocity survive.
	CHECK(kSceneColorFormat == PixelFormat::RGBA16_SFloat);
	CHECK(kVelocityFormat == PixelFormat::RGBA16_SFloat);

	// The ImGui sample view is the UNORM twin of the sRGB present image: same channels, different encoding.
	CHECK(kPresentColorFormat == PixelFormat::RGBA8_sRGB);
	CHECK(kPresentSampleFormat == PixelFormat::RGBA8_UNorm);
	CHECK(kPresentColorFormat != kPresentSampleFormat);
}
