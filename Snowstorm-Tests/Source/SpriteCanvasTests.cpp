#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Snowstorm/Render/SpriteCanvas.hpp"

#include <cstddef>

using namespace Snowstorm;
using Catch::Approx;

// The canvas fit is what keeps a game authored at one resolution correct in any window: uniform scale,
// centred, never stretched. These pin the three shapes (exact, wider, taller) and the degenerate inputs.
TEST_CASE("FitCanvas: exact match is identity", "[render][sprite]")
{
	const CanvasFit fit = FitCanvas(1920, 1080, {1920.0f, 1080.0f});
	CHECK(fit.Scale == Approx(1.0f));
	CHECK(fit.Offset.x == Approx(0.0f));
	CHECK(fit.Offset.y == Approx(0.0f));
}

TEST_CASE("FitCanvas: integer upscale keeps the canvas edge to edge", "[render][sprite]")
{
	const CanvasFit fit = FitCanvas(3840, 2160, {1920.0f, 1080.0f});
	CHECK(fit.Scale == Approx(2.0f));
	CHECK(fit.Offset.x == Approx(0.0f));
	CHECK(fit.Offset.y == Approx(0.0f));
}

TEST_CASE("FitCanvas: a wider target pillarboxes, centred", "[render][sprite]")
{
	const CanvasFit fit = FitCanvas(2560, 1080, {1920.0f, 1080.0f});
	CHECK(fit.Scale == Approx(1.0f));
	CHECK(fit.Offset.x == Approx(320.0f));
	CHECK(fit.Offset.y == Approx(0.0f));
}

TEST_CASE("FitCanvas: a taller target letterboxes, centred", "[render][sprite]")
{
	const CanvasFit fit = FitCanvas(1920, 1200, {1920.0f, 1080.0f});
	CHECK(fit.Scale == Approx(1.0f));
	CHECK(fit.Offset.x == Approx(0.0f));
	CHECK(fit.Offset.y == Approx(60.0f));
}

TEST_CASE("FitCanvas: the limiting axis decides the scale", "[render][sprite]")
{
	// 960x540 target is exactly half; 960x600 is still limited by width.
	CHECK(FitCanvas(960, 540, {1920.0f, 1080.0f}).Scale == Approx(0.5f));
	const CanvasFit fit = FitCanvas(960, 600, {1920.0f, 1080.0f});
	CHECK(fit.Scale == Approx(0.5f));
	CHECK(fit.Offset.y == Approx(30.0f));
}

TEST_CASE("FitCanvas: degenerate sizes yield scale 0 rather than dividing", "[render][sprite]")
{
	CHECK(FitCanvas(0, 1080, {1920.0f, 1080.0f}).Scale == Approx(0.0f));
	CHECK(FitCanvas(1920, 0, {1920.0f, 1080.0f}).Scale == Approx(0.0f));
	CHECK(FitCanvas(1920, 1080, {0.0f, 1080.0f}).Scale == Approx(0.0f));
	CHECK(FitCanvas(1920, 1080, {1920.0f, -1.0f}).Scale == Approx(0.0f));
}

TEST_CASE("MakeCanvasConstants carries the target size and the fit", "[render][sprite]")
{
	const SpriteCanvasConstants c = MakeCanvasConstants(2560, 1080, {1920.0f, 1080.0f});
	CHECK(c.TargetSize.x == Approx(2560.0f));
	CHECK(c.TargetSize.y == Approx(1080.0f));
	CHECK(c.CanvasScale == Approx(1.0f));
	CHECK(c.CanvasOffset.x == Approx(320.0f));
	CHECK(c.CanvasOffset.y == Approx(0.0f));
}

// The GPU-side structs are consumed by a StructuredBuffer stride and a cbuffer layout the shaders declare
// independently; the sizes are the contract.
TEST_CASE("Sprite GPU structs match the HLSL layouts", "[render][sprite]")
{
	STATIC_CHECK(sizeof(SpriteInstance) == 64);
	STATIC_CHECK(sizeof(SpriteCanvasConstants) == 32);
	STATIC_CHECK(offsetof(SpriteInstance, TextureIndex) == 48);
	STATIC_CHECK(offsetof(SpriteInstance, Rotation) == 52);
	STATIC_CHECK(offsetof(SpriteCanvasConstants, CanvasOffset) == 16);
}
