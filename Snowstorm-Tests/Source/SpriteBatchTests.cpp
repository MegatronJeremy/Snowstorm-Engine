#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/SpriteBatch.hpp"

#include <glm/ext/matrix_transform.hpp>

#include <vector>

using namespace Snowstorm;
using Catch::Approx;

namespace
{
	glm::vec2 Corner(const glm::mat4& model, const float u, const float v)
	{
		const glm::vec4 p = model * glm::vec4(u, v, 0.0f, 1.0f);
		return {p.x, p.y};
	}

	SpriteDraw At(const int32_t layer, const int32_t order, const uint32_t seq)
	{
		SpriteDraw d;
		d.Layer = layer;
		d.Order = order;
		d.Sequence = seq;
		return d;
	}
}

TEST_CASE("Sprites sort by layer, then order, then sequence", "[render][sprite]")
{
	std::vector<SpriteDraw> draws = {At(1, 0, 0), At(0, 5, 1), At(0, -2, 2), At(0, 5, 3), At(-1, 9, 4)};
	SortSpriteDraws(draws);

	REQUIRE(draws.size() == 5);
	CHECK(draws[0].Sequence == 4);
	CHECK(draws[1].Sequence == 2);
	CHECK(draws[2].Sequence == 1);
	CHECK(draws[3].Sequence == 3);
	CHECK(draws[4].Sequence == 0);
}

TEST_CASE("Position z does not order sprites", "[render][sprite]")
{
	SpriteDraw near = At(0, 0, 0);
	near.Transform = glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, 5.0f});
	SpriteDraw far = At(0, 0, 1);
	far.Transform = glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, -5.0f});

	std::vector<SpriteDraw> draws = {far, near};
	SortSpriteDraws(draws);
	CHECK(draws[0].Sequence == 0);
	CHECK(draws[1].Sequence == 1);
}

TEST_CASE("A centred pivot spans the size around the transform origin", "[render][sprite]")
{
	SpriteDraw d;
	d.Transform = glm::translate(glm::mat4(1.0f), {10.0f, 20.0f, 0.0f});
	d.Size = {4.0f, 2.0f};

	const glm::mat4 m = SpriteModelMatrix(d);
	CHECK(Corner(m, 0, 0).x == Approx(8.0f));
	CHECK(Corner(m, 0, 0).y == Approx(19.0f));
	CHECK(Corner(m, 1, 1).x == Approx(12.0f));
	CHECK(Corner(m, 1, 1).y == Approx(21.0f));
}

TEST_CASE("A bottom-left pivot puts the quad above and right of the origin", "[render][sprite]")
{
	SpriteDraw d;
	d.Size = {3.0f, 5.0f};
	d.Pivot = {0.0f, 0.0f};

	const glm::mat4 m = SpriteModelMatrix(d);
	CHECK(Corner(m, 0, 0).x == Approx(0.0f));
	CHECK(Corner(m, 0, 0).y == Approx(0.0f));
	CHECK(Corner(m, 1, 1).x == Approx(3.0f));
	CHECK(Corner(m, 1, 1).y == Approx(5.0f));
}

TEST_CASE("Flips mirror about the pivot", "[render][sprite]")
{
	SpriteDraw d;
	d.Size = {2.0f, 2.0f};
	d.Pivot = {0.0f, 0.0f};
	d.FlipX = true;

	const glm::mat4 m = SpriteModelMatrix(d);
	// The texture's left edge (u = 0) now sits at the pivot and the quad extends to -x.
	CHECK(Corner(m, 0, 0).x == Approx(0.0f));
	CHECK(Corner(m, 1, 0).x == Approx(-2.0f));
	CHECK(Corner(m, 1, 1).y == Approx(2.0f));

	d.FlipX = false;
	d.FlipY = true;
	const glm::mat4 my = SpriteModelMatrix(d);
	CHECK(Corner(my, 0, 1).y == Approx(-2.0f));
	CHECK(Corner(my, 1, 0).x == Approx(2.0f));
}

TEST_CASE("Transform scale multiplies the sprite size", "[render][sprite]")
{
	SpriteDraw d;
	d.Transform = glm::scale(glm::mat4(1.0f), {3.0f, 0.5f, 1.0f});
	d.Size = {2.0f, 4.0f};

	const glm::mat4 m = SpriteModelMatrix(d);
	CHECK(Corner(m, 1, 1).x - Corner(m, 0, 0).x == Approx(6.0f));
	CHECK(Corner(m, 1, 1).y - Corner(m, 0, 0).y == Approx(2.0f));
}

TEST_CASE("Instances carry texture, tint and filter in draw order", "[render][sprite]")
{
	SpriteDraw a = At(1, 0, 0);
	a.TextureIndex = 7;
	a.Linear = true;
	SpriteDraw b = At(0, 0, 1);
	b.TextureIndex = 3;
	b.Tint = {1.0f, 0.5f, 0.25f, 0.5f};

	std::vector<SpriteDraw> draws = {a, b};
	const std::vector<SpriteInstance> inst = BuildSpriteInstances(draws);

	REQUIRE(inst.size() == 2);
	CHECK(inst[0].TextureIndex == 3);
	CHECK(inst[0].Flags == 0u);
	CHECK(inst[0].Tint.y == Approx(0.5f));
	CHECK(inst[1].TextureIndex == 7);
	CHECK(inst[1].Flags == kSpriteFlagLinear);
}

TEST_CASE("A zero-size rect selects the whole texture", "[render][sprite]")
{
	const glm::vec4 whole{0.0f, 0.0f, 1.0f, 1.0f};
	CHECK(SpriteUvRect({0.0f, 0.0f, 0.0f, 0.0f}, 64, 64) == whole);
	CHECK(SpriteUvRect({8.0f, 8.0f, 0.0f, 16.0f}, 64, 64) == whole);
	CHECK(SpriteUvRect({8.0f, 8.0f, 16.0f, 0.0f}, 64, 64) == whole);
}

TEST_CASE("A texel rect maps to a UV offset and scale", "[render][sprite]")
{
	// Second 16x16 cell of the top row in a 64x32 sheet.
	const glm::vec4 uv = SpriteUvRect({16.0f, 0.0f, 16.0f, 16.0f}, 64, 32);
	CHECK(uv.x == Approx(0.25f));
	CHECK(uv.y == Approx(0.0f));
	CHECK(uv.z == Approx(0.25f));
	CHECK(uv.w == Approx(0.5f));
}

TEST_CASE("A rect clamps to the texture and degenerate rects fall back", "[render][sprite]")
{
	const glm::vec4 clamped = SpriteUvRect({48.0f, 16.0f, 32.0f, 32.0f}, 64, 32);
	CHECK(clamped.x == Approx(0.75f));
	CHECK(clamped.y == Approx(0.5f));
	CHECK(clamped.z == Approx(0.25f));
	CHECK(clamped.w == Approx(0.5f));

	const glm::vec4 whole{0.0f, 0.0f, 1.0f, 1.0f};
	CHECK(SpriteUvRect({100.0f, 0.0f, 8.0f, 8.0f}, 64, 32) == whole); // entirely outside
	CHECK(SpriteUvRect({-20.0f, 0.0f, 10.0f, 8.0f}, 64, 32) == whole);
	CHECK(SpriteUvRect({0.0f, 0.0f, 8.0f, 8.0f}, 0, 0) == whole); // size unknown
}

TEST_CASE("The rect reaches the instance and a default draw is the whole texture", "[render][sprite]")
{
	SpriteDraw plain;
	SpriteDraw cropped;
	cropped.Sequence = 1;
	cropped.UvRect = {0.5f, 0.0f, 0.5f, 0.5f};
	std::vector<SpriteDraw> draws = {plain, cropped};
	const auto inst = BuildSpriteInstances(draws);
	REQUIRE(inst.size() == 2);
	CHECK(inst[0].UvRect == glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
	CHECK(inst[1].UvRect == glm::vec4(0.5f, 0.0f, 0.5f, 0.5f));
}
