#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Systems/CameraControllerMath.hpp"

using namespace Snowstorm::CameraControllerMath;
using Catch::Approx;

namespace
{
	void RequireVec(const glm::vec3& v, const glm::vec3& e)
	{
		REQUIRE(v.x == Approx(e.x).margin(1e-5));
		REQUIRE(v.y == Approx(e.y).margin(1e-5));
		REQUIRE(v.z == Approx(e.z).margin(1e-5));
	}
}

TEST_CASE("CameraControllerMath: orientation", "[camera][controller]")
{
	SECTION("pitch 0 yaw 0 looks down -Z")
	{
		RequireVec(ForwardFromPitchYaw(0.0f, 0.0f), {0, 0, -1});
	}
	SECTION("+90 deg yaw turns left to -X")
	{
		RequireVec(ForwardFromPitchYaw(0.0f, glm::half_pi<float>()), {-1, 0, 0});
	}
	SECTION("positive pitch looks up, and yaw does not change the pitch angle")
	{
		RequireVec(ForwardFromPitchYaw(glm::half_pi<float>() * 0.5f, 0.0f),
		           {0, glm::sin(glm::quarter_pi<float>()), -glm::cos(glm::quarter_pi<float>())});
		RequireVec(ForwardFromPitchYaw(glm::quarter_pi<float>(), glm::half_pi<float>()),
		           {-glm::cos(glm::quarter_pi<float>()), glm::sin(glm::quarter_pi<float>()), 0});
	}
	SECTION("forward is unit length")
	{
		REQUIRE(glm::length(ForwardFromPitchYaw(0.7f, -2.3f)) == Approx(1.0f));
	}
	SECTION("right for default forward is +X and stays horizontal when looking up")
	{
		RequireVec(RightFromForward({0, 0, -1}), {1, 0, 0});
		const glm::vec3 r = RightFromForward(ForwardFromPitchYaw(1.0f, 0.4f));
		REQUIRE(r.y == Approx(0.0f).margin(1e-6));
		REQUIRE(glm::length(r) == Approx(1.0f));
	}
}

TEST_CASE("CameraControllerMath: SmoothAlpha", "[camera][controller]")
{
	REQUIRE(SmoothAlpha(0.0f, 0.016f) == 1.0f);
	REQUIRE(SmoothAlpha(-3.0f, 0.016f) == 1.0f);
	const float a = SmoothAlpha(10.0f, 0.016f);
	REQUIRE(a > 0.0f);
	REQUIRE(a < 1.0f);
	REQUIRE(SmoothAlpha(10.0f, 0.0f) == 0.0f);

	SECTION("two half steps equal one full step")
	{
		const float rate = 12.0f, dt = 0.05f;
		const float half = SmoothAlpha(rate, dt * 0.5f);
		const float twoHalves = 1.0f - (1.0f - half) * (1.0f - half);
		REQUIRE(twoHalves == Approx(SmoothAlpha(rate, dt)).epsilon(1e-5));
	}
	SECTION("two half steps toward a target land where one full step does")
	{
		const float rate = 8.0f, dt = 0.1f, target = 5.0f;
		float x = 1.0f;
		x += (target - x) * SmoothAlpha(rate, dt * 0.5f);
		x += (target - x) * SmoothAlpha(rate, dt * 0.5f);
		const float full = 1.0f + (target - 1.0f) * SmoothAlpha(rate, dt);
		REQUIRE(x == Approx(full).epsilon(1e-5));
	}
}

TEST_CASE("CameraControllerMath: pitch clamp", "[camera][controller]")
{
	constexpr float limit = glm::half_pi<float>() - 0.001f;
	REQUIRE(ClampLookPitch(0.3f) == 0.3f);
	REQUIRE(ClampLookPitch(10.0f) == Approx(limit));
	REQUIRE(ClampLookPitch(-10.0f) == Approx(-limit));
	REQUIRE(ClampLookPitch(glm::half_pi<float>()) < glm::half_pi<float>());
}

TEST_CASE("CameraControllerMath: move composition", "[camera][controller]")
{
	const glm::vec3 fwd{0, 0, -1}, right{1, 0, 0};

	SECTION("perspective: W forward, E up, D right")
	{
		RequireVec(ComposeMoveDir({.W = true}, fwd, right, true), fwd);
		RequireVec(ComposeMoveDir({.S = true}, fwd, right, true), -fwd);
		RequireVec(ComposeMoveDir({.E = true}, fwd, right, true), {0, 1, 0});
		RequireVec(ComposeMoveDir({.Q = true}, fwd, right, true), {0, -1, 0});
		RequireVec(ComposeMoveDir({.D = true}, fwd, right, true), right);
		RequireVec(ComposeMoveDir({.A = true}, fwd, right, true), -right);
	}
	SECTION("orthographic: W up, E forward")
	{
		RequireVec(ComposeMoveDir({.W = true}, fwd, right, false), {0, 1, 0});
		RequireVec(ComposeMoveDir({.S = true}, fwd, right, false), {0, -1, 0});
		RequireVec(ComposeMoveDir({.E = true}, fwd, right, false), fwd);
		RequireVec(ComposeMoveDir({.Q = true}, fwd, right, false), -fwd);
		RequireVec(ComposeMoveDir({.D = true}, fwd, right, false), right);
	}
	SECTION("opposing keys cancel")
	{
		const glm::vec3 d = ComposeMoveDir({.W = true, .A = true, .S = true, .D = true}, fwd, right, true);
		RequireVec(d, {0, 0, 0});
		RequireVec(TargetVelocity(d, 5.0f), {0, 0, 0});
	}
	SECTION("diagonal is normalized to the speed")
	{
		const glm::vec3 v = TargetVelocity(ComposeMoveDir({.W = true, .D = true}, fwd, right, true), 4.0f);
		REQUIRE(glm::length(v) == Approx(4.0f));
	}
}
