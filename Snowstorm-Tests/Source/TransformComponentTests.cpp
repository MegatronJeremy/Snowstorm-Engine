#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/ECS/TrackedRegistry.hpp"
#include "Snowstorm/Math/EulerContinuity.hpp"

#include <algorithm>
#include <cmath>

using namespace Snowstorm;
using Catch::Approx;

namespace
{
	bool NearlyEqual(const glm::mat4& a, const glm::mat4& b, const float eps = 1e-5f)
	{
		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r)
				if (std::abs(a[c][r] - b[c][r]) > eps)
					return false;
		return true;
	}

	bool NearlyEqual(const glm::vec3& a, const glm::vec3& b, const float eps = 1e-5f)
	{
		return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(eps)));
	}

	TransformComponent Make(const glm::vec3& p, const glm::vec3& r, const glm::vec3& s)
	{
		TransformComponent t;
		t.Position = p;
		t.Rotation = r;
		t.Scale = s;
		return t;
	}
}

TEST_CASE("TransformComponent: defaults give the identity matrix", "[transform]")
{
	const TransformComponent t;
	CHECK(NearlyEqual(t.GetTransformMatrix(), glm::mat4(1.0f)));
}

TEST_CASE("TransformComponent: the implicit conversion equals GetTransformMatrix", "[transform]")
{
	const TransformComponent t = Make({1, 2, 3}, {0.3f, -0.4f, 0.5f}, {2, 3, 4});
	const glm::mat4 viaCast = t;
	CHECK(NearlyEqual(viaCast, t.GetTransformMatrix(), 0.0f));
}

TEST_CASE("TransformComponent: the matrix is T * Ry * Rx * Rz * S", "[transform]")
{
	const TransformComponent t = Make({1, -2, 3}, {0.4f, 0.9f, -0.6f}, {2, 3, 0.5f});

	const glm::mat4 expected = glm::translate(glm::mat4(1.0f), t.Position) *
	                           glm::rotate(glm::mat4(1.0f), t.Rotation.y, {0, 1, 0}) *
	                           glm::rotate(glm::mat4(1.0f), t.Rotation.x, {1, 0, 0}) *
	                           glm::rotate(glm::mat4(1.0f), t.Rotation.z, {0, 0, 1}) *
	                           glm::scale(glm::mat4(1.0f), t.Scale);

	CHECK(NearlyEqual(t.GetTransformMatrix(), expected));
}

TEST_CASE("TransformComponent: translation lands in the fourth column", "[transform]")
{
	const glm::mat4 m = Make({4, 5, 6}, {0, 0, 0}, {1, 1, 1}).GetTransformMatrix();
	CHECK(NearlyEqual(glm::vec3(m[3]), {4, 5, 6}));
	CHECK(m[3].w == Approx(1.0f));
}

TEST_CASE("TransformComponent: non-uniform scale applies before rotation and translation", "[transform]")
{
	// Yaw 90 degrees maps +X to -Z. A point on the scaled X axis must be stretched first, then turned.
	const glm::mat4 m = Make({10, 0, 0}, {0, glm::half_pi<float>(), 0}, {2, 3, 4}).GetTransformMatrix();

	CHECK(NearlyEqual(glm::vec3(m * glm::vec4(1, 0, 0, 1)), {10, 0, -2}));
	CHECK(NearlyEqual(glm::vec3(m * glm::vec4(0, 1, 0, 1)), {10, 3, 0}));
	CHECK(NearlyEqual(glm::vec3(m * glm::vec4(0, 0, 1, 1)), {14, 0, 0}));
}

TEST_CASE("TransformComponent: column lengths of the upper 3x3 are the scale", "[transform]")
{
	const glm::vec3 scale{2.0f, 3.0f, 0.5f};
	const glm::mat4 m = Make({1, 2, 3}, {0.7f, -1.1f, 2.0f}, scale).GetTransformMatrix();

	CHECK(glm::length(glm::vec3(m[0])) == Approx(scale.x));
	CHECK(glm::length(glm::vec3(m[1])) == Approx(scale.y));
	CHECK(glm::length(glm::vec3(m[2])) == Approx(scale.z));
}

TEST_CASE("TransformComponent: rotation order is yaw, then pitch, then roll about local axes", "[transform]")
{
	// Ry*Rx*Rz applied to a vector rolls first. +X rolled 90 deg about Z is +Y, pitch 90 about X turns
	// +Y into +Z, yaw 90 about Y turns +Z into +X.
	const glm::mat4 m = Make({0, 0, 0}, {glm::half_pi<float>(), glm::half_pi<float>(), glm::half_pi<float>()},
	                         {1, 1, 1})
	                        .GetTransformMatrix();
	CHECK(NearlyEqual(glm::vec3(m * glm::vec4(1, 0, 0, 0)), {1, 0, 0}));

	// The same angles in another order do not agree, so the order is observable.
	const glm::mat4 swapped = glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), {0, 0, 1}) *
	                          glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), {1, 0, 0}) *
	                          glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), {0, 1, 0});
	CHECK_FALSE(NearlyEqual(m, swapped));
}

TEST_CASE("TransformComponent: Euler angles read back from the matrix within tolerance", "[transform][euler]")
{
	for (const glm::vec3 e : {glm::vec3{0.3f, 1.0f, -0.7f}, glm::vec3{-1.2f, 2.5f, 0.4f}, glm::vec3{0.1f, -3.0f, 1.5f}})
	{
		const TransformComponent t = Make({}, e, {1, 1, 1});
		const glm::vec3 back = NearestEulerYXZ(t.GetTransformMatrix(), e);
		CHECK(NearlyEqual(back, e, 1e-3f));
	}
}

TEST_CASE("TransformComponent: yaw stays continuous across the plus-minus 180 degree wrap", "[transform][euler]")
{
	glm::vec3 current{0.2f, glm::radians(170.0f), 0.1f};
	float maxStep = 0.0f;
	for (int i = 0; i <= 200; ++i)
	{
		// Yaw sweeps 170 -> 190 degrees; the raw angle passes +180 where an extractor would flip to -180.
		const float yaw = glm::radians(170.0f + 20.0f * static_cast<float>(i) / 200.0f);
		const TransformComponent t = Make({}, {0.2f, yaw, 0.1f}, {1, 1, 1});
		const glm::vec3 next = NearestEulerYXZ(t.GetTransformMatrix(), current);

		CHECK(NearlyEqual(glm::mat3(Make({}, next, {1, 1, 1}).GetTransformMatrix())[0],
		                  glm::mat3(t.GetTransformMatrix())[0], 1e-3f));
		maxStep = std::max(maxStep, glm::length(next - current));
		current = next;
	}
	CHECK(maxStep < glm::radians(1.0f));
	CHECK(current.y == Approx(glm::radians(190.0f)).margin(1e-3));
}

TEST_CASE("TransformComponent: a tracked write marks Changed, a read and an untracked write do not", "[transform][ecs]")
{
	TrackedRegistry reg;
	const auto e = reg.create();
	reg.emplace<TransformComponent>(e);
	reg.ClearTrackedComponents();

	(void)reg.Read<TransformComponent>(e);
	reg.get<TransformComponent>(e).Position.x = 5.0f;
	CHECK_FALSE(reg.WasChanged<TransformComponent>(e));
	CHECK(reg.ChangedView<TransformComponent>().empty());

	reg.Write<TransformComponent>(e).Scale = {2, 2, 2};
	CHECK(reg.WasChanged<TransformComponent>(e));
	CHECK(reg.ChangedView<TransformComponent>().contains(e));
	CHECK(reg.Read<TransformComponent>(e).Scale == glm::vec3(2, 2, 2));

	reg.ClearTrackedComponents();
	CHECK_FALSE(reg.WasChanged<TransformComponent>(e));
}

TEST_CASE("TransformComponent: patch marks Changed and a new component counts as added and changed",
          "[transform][ecs]")
{
	TrackedRegistry reg;
	const auto e = reg.create();
	reg.emplace<TransformComponent>(e);

	CHECK(reg.WasAdded<TransformComponent>(e));
	CHECK(reg.WasChanged<TransformComponent>(e));

	reg.ClearTrackedComponents();
	reg.patch<TransformComponent>(e, [](TransformComponent& t)
	                              { t.Rotation.y = 1.0f; });
	CHECK(reg.WasChanged<TransformComponent>(e));
	CHECK_FALSE(reg.WasAdded<TransformComponent>(e));
	CHECK(reg.Read<TransformComponent>(e).Rotation.y == Approx(1.0f));
}
