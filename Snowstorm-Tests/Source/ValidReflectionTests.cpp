#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <random>

#include <glm/glm.hpp>

#include "Snowstorm/Render/ValidReflection.hpp"

using namespace Snowstorm;
using Catch::Approx;

namespace
{
	const glm::vec3 kNg{0.0f, 0.0f, 1.0f};

	glm::vec3 Reflect(const glm::vec3& N, const glm::vec3& I)
	{
		return 2.0f * glm::dot(N, I) * N - I;
	}

	float Threshold(const glm::vec3& I)
	{
		return std::min(0.9f * glm::dot(kNg, I), 0.01f);
	}
}

TEST_CASE("valid reflection: a normal whose reflection is already above the surface is returned unchanged", "[validreflection]")
{
	const glm::vec3 I = glm::normalize(glm::vec3(0.3f, 0.0f, 0.8f));
	const glm::vec3 N = glm::normalize(glm::vec3(0.1f, 0.0f, 1.0f));
	const glm::vec3 out = ValidReflection::Apply(kNg, I, N);
	CHECK(out.x == N.x);
	CHECK(out.y == N.y);
	CHECK(out.z == N.z);
}

TEST_CASE("valid reflection: a grazing view with a tilted normal is rotated towards Ng until R clears the threshold", "[validreflection]")
{
	const glm::vec3 I = glm::normalize(glm::vec3(0.9f, 0.0f, 0.436f));
	const glm::vec3 N = glm::normalize(glm::vec3(-0.3f, 0.0f, 0.954f));
	REQUIRE(glm::dot(N, I) > 0.0f);
	REQUIRE(glm::dot(kNg, Reflect(N, I)) < Threshold(I));

	const glm::vec3 out = ValidReflection::Apply(kNg, I, N);
	CHECK(glm::length(out) == Approx(1.0f).margin(1e-4f));
	CHECK(glm::dot(kNg, Reflect(out, I)) >= Threshold(I) - 1e-4f);
	CHECK(glm::dot(out, kNg) > glm::dot(N, kNg));
}

TEST_CASE("valid reflection: the result is unit length, in the plane of N and Ng, and its reflection clears the threshold", "[validreflection]")
{
	std::mt19937 rng(12345);
	std::uniform_real_distribution<float> u(-1.0f, 1.0f);
	int rotated = 0;
	for (int i = 0; i < 20000; ++i)
	{
		const glm::vec3 I = glm::normalize(glm::vec3(u(rng), u(rng), std::fabs(u(rng)) * 0.3f + 0.001f));
		const glm::vec3 N = glm::normalize(glm::vec3(u(rng) * 0.8f, u(rng) * 0.8f, 0.2f + std::fabs(u(rng))));
		if (glm::dot(N, I) < 1e-3f)
		{
			continue;
		}
		const glm::vec3 out = ValidReflection::Apply(kNg, I, N);
		REQUIRE(glm::length(out) == Approx(1.0f).margin(1e-3f));
		REQUIRE(glm::dot(kNg, Reflect(out, I)) >= Threshold(I) - 1e-3f);
		// N, Ng and out are coplanar: the triple product vanishes.
		REQUIRE(glm::dot(glm::cross(N, kNg), out) == Approx(0.0f).margin(1e-3f));
		rotated += glm::dot(out, N) < 0.9999f ? 1 : 0;
	}
	CHECK(rotated > 0);
}
