#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/TransparentSort.hpp"

#include <vector>

using namespace Snowstorm;

namespace
{
	TransparentDraw Draw(const glm::vec3& centre, const uint32_t seq)
	{
		TransparentDraw d;
		d.BoundsCenter = centre;
		d.Sequence = seq;
		return d;
	}
}

TEST_CASE("Transparent draws sort far to near along the view axis", "[render][transparent]")
{
	std::vector<TransparentDraw> draws = {Draw({0, 0, -2}, 0), Draw({0, 0, -9}, 1), Draw({0, 0, -5}, 2)};
	SortTransparentDraws(draws, {0, 0, 0}, {0, 0, -1});

	REQUIRE(draws.size() == 3);
	CHECK(draws[0].Sequence == 1);
	CHECK(draws[1].Sequence == 2);
	CHECK(draws[2].Sequence == 0);
	CHECK(draws[0].ViewDepth == 9.0f);
}

TEST_CASE("Transparent sort uses depth along the axis, not distance", "[render][transparent]")
{
	// B is nearer in Euclidean distance but deeper along the view axis.
	std::vector<TransparentDraw> draws = {Draw({50, 0, -4}, 0), Draw({0, 0, -6}, 1)};
	SortTransparentDraws(draws, {0, 0, 0}, {0, 0, -1});
	CHECK(draws[0].Sequence == 1);
}

TEST_CASE("Equal depth keeps submission order", "[render][transparent]")
{
	std::vector<TransparentDraw> draws = {Draw({3, 0, -4}, 2), Draw({-3, 0, -4}, 0), Draw({0, 5, -4}, 1)};
	SortTransparentDraws(draws, {0, 0, 0}, {0, 0, -1});
	CHECK(draws[0].Sequence == 0);
	CHECK(draws[1].Sequence == 1);
	CHECK(draws[2].Sequence == 2);
}

TEST_CASE("Draws behind the camera sort after every visible draw", "[render][transparent]")
{
	std::vector<TransparentDraw> draws = {Draw({0, 0, 3}, 0), Draw({0, 0, -1}, 1), Draw({0, 0, -20}, 2)};
	SortTransparentDraws(draws, {0, 0, 0}, {0, 0, -1});
	CHECK(draws[0].Sequence == 2);
	CHECK(draws[1].Sequence == 1);
	CHECK(draws[2].Sequence == 0);
	CHECK(draws[2].ViewDepth < 0.0f);
}

TEST_CASE("Transparent sort handles an empty list and a moved camera", "[render][transparent]")
{
	std::vector<TransparentDraw> none;
	SortTransparentDraws(none, {0, 0, 0}, {0, 0, -1});
	CHECK(none.empty());

	std::vector<TransparentDraw> draws = {Draw({0, 0, -2}, 0), Draw({0, 0, -9}, 1)};
	SortTransparentDraws(draws, {0, 0, -20}, {0, 0, 1}); // looking back down +z
	CHECK(draws[0].Sequence == 0);
}
