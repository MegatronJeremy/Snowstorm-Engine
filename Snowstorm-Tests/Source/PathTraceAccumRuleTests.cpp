#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/PathTraceAccumRule.hpp"

using namespace Snowstorm::PathTraceAccumRule;

namespace
{
	Now Pose(const glm::mat4& vp, const uint32_t spp = 4)
	{
		Now n;
		n.ViewProjection = vp;
		n.SamplesPerFrame = spp;
		return n;
	}
}

TEST_CASE("Accumulation: the first frame resets against the zero matrix", "[pathtrace]")
{
	const Decision d = Decide(Last{}, Pose(glm::mat4(1.0f)));
	REQUIRE(d.Reset);
	REQUIRE(d.BaseSamples == 0);
	REQUIRE(d.NextSamples == 4);
}

TEST_CASE("Accumulation: an identical pose keeps adding samples", "[pathtrace]")
{
	Last last{glm::mat4(1.0f), 8, 1, 0};
	const Decision d = Decide(last, Pose(glm::mat4(1.0f)));
	REQUIRE_FALSE(d.Reset);
	REQUIRE(d.BaseSamples == 8);
	REQUIRE(d.NextSamples == 12);
}

TEST_CASE("Accumulation: any view-projection change resets", "[pathtrace]")
{
	glm::mat4 moved(1.0f);
	moved[3][0] = 1e-6f;
	const Decision d = Decide(Last{glm::mat4(1.0f), 64, 1, 0}, Pose(moved));
	REQUIRE(d.Reset);
	REQUIRE(d.BaseSamples == 0);
	REQUIRE(d.NextSamples == 4);
}

TEST_CASE("Accumulation: streaming assets hold the mean at one frame", "[pathtrace]")
{
	Now n = Pose(glm::mat4(1.0f));
	n.SceneSettling = true;
	const Decision d = Decide(Last{glm::mat4(1.0f), 64, 1, 0}, n);
	REQUIRE(d.Reset);
	REQUIRE(d.NextSamples == 4);
}

TEST_CASE("Accumulation: flipping env-NEE or the sampler resets", "[pathtrace]")
{
	const Last last{glm::mat4(1.0f), 32, 1, 0};
	Now n = Pose(glm::mat4(1.0f));
	n.EnvNee = 0;
	REQUIRE(Decide(last, n).Reset);
	n.EnvNee = 1;
	n.Sampler = 1;
	REQUIRE(Decide(last, n).Reset);
	n.Sampler = 0;
	REQUIRE_FALSE(Decide(last, n).Reset);
}

TEST_CASE("Accumulation: a settling scene never accumulates across frames", "[pathtrace]")
{
	Last last{};
	Now n = Pose(glm::mat4(1.0f), 2);
	n.SceneSettling = true;
	for (int i = 0; i < 3; ++i)
	{
		const Decision d = Decide(last, n);
		last = {n.ViewProjection, d.NextSamples, n.EnvNee, n.Sampler};
		REQUIRE(d.NextSamples == 2);
	}
	n.SceneSettling = false;
	REQUIRE(Decide(last, n).NextSamples == 4);
}
