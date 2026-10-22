#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Snowstorm/Render/PixelPerfect.hpp"

using namespace Snowstorm;
using Catch::Approx;

TEST_CASE("PixelPerfectScale is the largest integer that fits both axes", "[render][pixelperfect]")
{
	CHECK(PixelPerfectScale(1920, 1080, 320, 180) == 6);
	CHECK(PixelPerfectScale(1920, 1080, 640, 360) == 3);
	// The tighter axis decides: 1000/320 = 3.1, 1080/180 = 6.
	CHECK(PixelPerfectScale(1000, 1080, 320, 180) == 3);
	CHECK(PixelPerfectScale(1919, 1079, 320, 180) == 5);
}

TEST_CASE("PixelPerfectScale never drops below 1", "[render][pixelperfect]")
{
	CHECK(PixelPerfectScale(200, 100, 320, 180) == 1);
	CHECK(PixelPerfectScale(1920, 1080, 0, 0) == 1);
}

TEST_CASE("PixelPerfectScale with one axis unset follows the other", "[render][pixelperfect]")
{
	CHECK(PixelPerfectScale(1920, 1080, 0, 180) == 6);
	CHECK(PixelPerfectScale(1920, 1080, 480, 0) == 4);
}

TEST_CASE("PixelPerfectOrthographicSize puts a whole number of pixels on each texel", "[render][pixelperfect]")
{
	const float ppu = 16.0f;
	const int scale = 6;
	const float h = 1080.0f;
	const float size = PixelPerfectOrthographicSize(h, ppu, scale);
	CHECK(size == Approx(11.25f));
	// Pixels per world unit on screen = viewport height / size = ppu * scale.
	CHECK(h / size == Approx(ppu * scale));
	CHECK(PixelPerfectOrthographicSize(h, ppu, 0) == Approx(h / ppu));
}

TEST_CASE("SnapToTexel lands on the texel grid", "[render][pixelperfect]")
{
	CHECK(SnapToTexel(0.0f, 16.0f) == Approx(0.0f));
	CHECK(SnapToTexel(0.03f, 16.0f) == Approx(0.0f));
	CHECK(SnapToTexel(0.04f, 16.0f) == Approx(0.0625f));
	CHECK(SnapToTexel(-0.04f, 16.0f) == Approx(-0.0625f));
	CHECK(SnapToTexel(1.0f, 16.0f) == Approx(1.0f));
}
