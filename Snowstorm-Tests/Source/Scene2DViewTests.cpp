#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include "Snowstorm/Math/Scene2DView.hpp"

using namespace Snowstorm;
using Catch::Approx;

TEST_CASE("Scene2DView: pan moves the camera opposite the drag, y flipped", "[editor][scene2d]")
{
	// 10 world units over 500 px = 0.02 per pixel.
	const glm::vec2 d = Scene2DView::PanDelta({50.0f, 25.0f}, 10.0f, 500.0f);
	CHECK(d.x == Approx(-1.0f));
	CHECK(d.y == Approx(0.5f));
	CHECK(Scene2DView::PanDelta({5.0f, 5.0f}, 10.0f, 0.0f) == glm::vec2(0.0f));
}

TEST_CASE("Scene2DView: zoom about the cursor keeps the point under it fixed", "[editor][scene2d]")
{
	const glm::vec2 center{3.0f, -2.0f};
	const glm::vec2 uv{0.8f, 0.3f};
	constexpr float aspect = 16.0f / 9.0f;
	const float oldSize = 10.0f;
	const float newSize = Scene2DView::ZoomedSize(oldSize, 2.0f, 0.1f);
	CHECK(newSize == Approx(8.0f));

	const glm::vec2 newCenter = Scene2DView::CenterAfterZoom(center, oldSize, newSize, uv, aspect);
	const glm::vec2 before = center + Scene2DView::CursorOffset(uv, oldSize, aspect);
	const glm::vec2 after = newCenter + Scene2DView::CursorOffset(uv, newSize, aspect);
	CHECK(after.x == Approx(before.x));
	CHECK(after.y == Approx(before.y));

	// A cursor at the centre never moves the camera.
	const glm::vec2 c = Scene2DView::CenterAfterZoom(center, oldSize, newSize, {0.5f, 0.5f}, aspect);
	CHECK(c.x == Approx(center.x));
	CHECK(c.y == Approx(center.y));
}

TEST_CASE("Scene2DView: zoom clamps and a wheel burst cannot invert the size", "[editor][scene2d]")
{
	CHECK(Scene2DView::ZoomedSize(10.0f, 1000.0f, 0.1f) == Approx(2.0f));   // factor floored at 0.2
	CHECK(Scene2DView::ZoomedSize(10.0f, -1000.0f, 0.1f) == Approx(50.0f)); // factor capped at 5
	CHECK(Scene2DView::ZoomedSize(0.06f, 5.0f, 0.1f) == Approx(Scene2DView::kMinSize));
	CHECK(Scene2DView::ZoomedSize(9000.0f, -5.0f, 0.1f) == Approx(Scene2DView::kMaxSize));
}

TEST_CASE("Scene2DView: framing fits tall and wide bounds", "[editor][scene2d]")
{
	const auto tall = Scene2DView::FrameBounds({-1.0f, 0.0f}, {1.0f, 8.0f}, 2.0f);
	CHECK(tall.Center == glm::vec2(0.0f, 4.0f));
	CHECK(tall.Size == Approx(8.0f * Scene2DView::kFrameMargin));

	// 20 wide at aspect 2 needs a height of 10, more than the 4 high extent.
	const auto wide = Scene2DView::FrameBounds({0.0f, 0.0f}, {20.0f, 4.0f}, 2.0f);
	CHECK(wide.Size == Approx(10.0f * Scene2DView::kFrameMargin));

	const auto point = Scene2DView::FrameBounds({2.0f, 2.0f}, {2.0f, 2.0f}, 1.0f);
	CHECK(point.Center == glm::vec2(2.0f));
	CHECK(point.Size == Approx(1.0f));
}

TEST_CASE("Scene2DView: sprite bounds follow size and pivot through the transform", "[editor][scene2d]")
{
	const AABB local = Scene2DView::SpriteLocalAABB({4.0f, 2.0f}, {0.25f, 0.5f});
	CHECK(local.Min == glm::vec3(-1.0f, -1.0f, 0.0f));
	CHECK(local.Max == glm::vec3(3.0f, 1.0f, 0.0f));

	// Scale 2 then translate (10, 5): the quad spans x 8..16, y 3..7.
	glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 5.0f, 0.0f));
	m = glm::scale(m, glm::vec3(2.0f, 2.0f, 1.0f));
	const AABB w = TransformAABB(local, m);
	CHECK(w.Min.x == Approx(8.0f));
	CHECK(w.Max.x == Approx(16.0f));
	CHECK(w.Min.y == Approx(3.0f));
	CHECK(w.Max.y == Approx(7.0f));
}
