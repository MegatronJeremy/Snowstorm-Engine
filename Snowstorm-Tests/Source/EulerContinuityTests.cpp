#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Math/EulerContinuity.hpp"

#include <algorithm>

using namespace Snowstorm;
using Catch::Approx;

namespace
{
	glm::mat4 FromEuler(const glm::vec3& e)
	{
		return glm::eulerAngleYXZ(e.y, e.x, e.z);
	}

	bool SameOrientation(const glm::mat4& a, const glm::mat4& b)
	{
		for (int c = 0; c < 3; ++c)
			for (int r = 0; r < 3; ++r)
				if (std::abs(a[c][r] - b[c][r]) > 2e-3f)
					return false;
		return true;
	}
}

TEST_CASE("NearestEulerYXZ reproduces the orientation", "[math][euler]")
{
	for (const glm::vec3 e : {glm::vec3{0.3f, 1.0f, -0.7f}, glm::vec3{-1.2f, 2.5f, 0.4f}, glm::vec3{1.4f, -0.5f, 3.0f}})
	{
		const glm::vec3 r = NearestEulerYXZ(FromEuler(e), e);
		CHECK(SameOrientation(FromEuler(r), FromEuler(e)));
		CHECK(r.x == Approx(e.x).margin(1e-3));
		CHECK(r.y == Approx(e.y).margin(1e-3));
		CHECK(r.z == Approx(e.z).margin(1e-3));
	}
}

TEST_CASE("NearestEulerYXZ stays continuous across the pitch lock", "[math][euler]")
{
	// Sweep pitch through -90 degrees with a fixed yaw/roll, feeding each result back as `previous`.
	glm::vec3 current{glm::radians(-80.0f), 0.6f, 0.25f};
	float maxStep = 0.0f;
	for (int i = 0; i <= 400; ++i)
	{
		const float pitch = glm::radians(-80.0f - 20.0f * static_cast<float>(i) / 400.0f);
		const glm::mat4 m = FromEuler({pitch, 0.6f, 0.25f});
		const glm::vec3 next = NearestEulerYXZ(m, current);
		CHECK(SameOrientation(FromEuler(next), m));
		maxStep = std::max(maxStep, glm::length(next - current));
		current = next;
	}
	// 20 degrees over 400 steps is 0.05 degrees per step; allow far less than any 180 degree flip.
	CHECK(maxStep < glm::radians(2.0f));
}

TEST_CASE("NearestEulerYXZ unwraps to the previous branch", "[math][euler]")
{
	const glm::vec3 previous{0.1f, 6.2f, 0.0f}; // yaw just past one turn
	const glm::vec3 r = NearestEulerYXZ(FromEuler({0.1f, 6.2f - glm::two_pi<float>() + 0.05f, 0.0f}), previous);
	CHECK(r.y == Approx(6.25f).margin(1e-3));
}

TEST_CASE("NearestEulerYXZ at exact lock keeps the previous roll", "[math][euler]")
{
	const glm::vec3 previous{glm::half_pi<float>(), 0.4f, 0.9f};
	const glm::mat4 m = FromEuler({glm::half_pi<float>(), 0.7f, 0.9f});
	const glm::vec3 r = NearestEulerYXZ(m, previous);
	CHECK(SameOrientation(FromEuler(r), m));
	CHECK(r.z == Approx(0.9f).margin(1e-3));
}
