#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "Snowstorm/Render/IBLInvalidation.hpp"

using namespace Snowstorm;

namespace
{
	LightDataBlock SunBlock(glm::vec3 direction, glm::vec3 color = {1, 1, 1}, float intensity = 3.0f)
	{
		LightDataBlock b;
		b.LightCount = 1;
		b.Lights[0].Direction = direction;
		b.Lights[0].Color = color;
		b.Lights[0].Intensity = intensity;
		return b;
	}

	glm::vec3 Rotated(float deg)
	{
		const float r = glm::radians(deg);
		return {std::sin(r), -std::cos(r), 0.0f};
	}

	constexpr float kQuantum = 2.0f;
	constexpr float kTol = 0.05f;
}

TEST_CASE("Sun key reads Lights[0] and negates the direction", "[render][ibl]")
{
	const IBLSunKey k = MakeIBLSunKey(SunBlock({0, -2, 0}, {1, 0.5f, 0.25f}, 4.0f));
	REQUIRE(k.Present);
	REQUIRE(k.ToSun.y == 1.0f);
	REQUIRE(k.Radiance.y == 2.0f);
}

TEST_CASE("No lights or a zero direction is no sun", "[render][ibl]")
{
	REQUIRE_FALSE(MakeIBLSunKey(LightDataBlock{}).Present);
	REQUIRE_FALSE(MakeIBLSunKey(SunBlock({0, 0, 0})).Present);
}

TEST_CASE("Sun movement under the quantum does not re-bake, over it does", "[render][ibl]")
{
	const IBLSunKey baked = MakeIBLSunKey(SunBlock(Rotated(0)));
	REQUIRE_FALSE(SunNeedsRebake(baked, MakeIBLSunKey(SunBlock(Rotated(1.5f))), kQuantum, kTol));
	REQUIRE(SunNeedsRebake(baked, MakeIBLSunKey(SunBlock(Rotated(2.5f))), kQuantum, kTol));
}

TEST_CASE("Drift accumulates against the baked key", "[render][ibl]")
{
	const IBLSunKey baked = MakeIBLSunKey(SunBlock(Rotated(0)));
	bool fired = false;
	for (int step = 1; step <= 10 && !fired; ++step)
		fired = SunNeedsRebake(baked, MakeIBLSunKey(SunBlock(Rotated(0.5f * static_cast<float>(step)))), kQuantum, kTol);
	REQUIRE(fired);
}

TEST_CASE("Sun colour and intensity re-bake past the relative tolerance", "[render][ibl]")
{
	const IBLSunKey baked = MakeIBLSunKey(SunBlock(Rotated(0), {1, 1, 1}, 3.0f));
	REQUIRE_FALSE(SunNeedsRebake(baked, MakeIBLSunKey(SunBlock(Rotated(0), {1, 1, 1}, 3.1f)), kQuantum, kTol));
	REQUIRE(SunNeedsRebake(baked, MakeIBLSunKey(SunBlock(Rotated(0), {1, 1, 1}, 4.0f)), kQuantum, kTol));
	REQUIRE(SunNeedsRebake(baked, MakeIBLSunKey(SunBlock(Rotated(0), {1, 0.5f, 0.2f}, 3.0f)), kQuantum, kTol));
}

TEST_CASE("Sun appearing or disappearing re-bakes", "[render][ibl]")
{
	const IBLSunKey none = MakeIBLSunKey(LightDataBlock{});
	const IBLSunKey sun = MakeIBLSunKey(SunBlock(Rotated(0)));
	REQUIRE(SunNeedsRebake(none, sun, kQuantum, kTol));
	REQUIRE(SunNeedsRebake(sun, none, kQuantum, kTol));
	REQUIRE_FALSE(SunNeedsRebake(none, none, kQuantum, kTol));
}

TEST_CASE("Quantum 0 re-bakes on any direction change", "[render][ibl]")
{
	const IBLSunKey baked = MakeIBLSunKey(SunBlock(Rotated(0)));
	REQUIRE(SunNeedsRebake(baked, MakeIBLSunKey(SunBlock(Rotated(0.1f))), 0.0f, kTol));
	REQUIRE_FALSE(SunNeedsRebake(baked, baked, 0.0f, kTol));
}

TEST_CASE("SunAngleDeg measures the angle between two suns and is 0 without one", "[ibl]")
{
	const IBLSunKey a = MakeIBLSunKey(SunBlock(Rotated(0)));
	REQUIRE(SunAngleDeg(a, MakeIBLSunKey(SunBlock(Rotated(2.5f)))) == Catch::Approx(2.5f).margin(1e-3f));
	REQUIRE(SunAngleDeg(a, a) == Catch::Approx(0.0f).margin(1e-3f));
	REQUIRE(SunAngleDeg(a, IBLSunKey{}) == 0.0f);
}

TEST_CASE("IBLRebakeStats accumulates count, mean and max stall", "[ibl]")
{
	IBLRebakeStats s;
	REQUIRE(s.MeanStallMs() == 0.0);
	s.Record(2.0, true);
	s.Record(6.0, false);
	REQUIRE(s.Count == 2);
	REQUIRE(s.SunTriggered == 1);
	REQUIRE(s.MeanStallMs() == Catch::Approx(4.0));
	REQUIRE(s.MaxStallMs == Catch::Approx(6.0));
}
