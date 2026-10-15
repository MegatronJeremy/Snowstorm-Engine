#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include "Snowstorm/Render/CascadeMath.hpp"

using namespace Snowstorm;
using Catch::Approx;

namespace
{
	constexpr float kNear = 0.5f;
	constexpr float kFar = 200.0f;

	glm::mat4 CameraViewProj(const glm::vec3& pos, float yaw = 0.0f)
	{
		const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(60.0f), 16.0f / 9.0f, kNear, kFar);
		const glm::mat4 view = glm::rotate(glm::mat4(1.0f), -yaw, glm::vec3(0, 1, 0)) * glm::translate(glm::mat4(1.0f), -pos);
		return proj * view;
	}

	const glm::vec3 kSun = glm::normalize(glm::vec3(0.3f, -1.0f, 0.2f));
}

TEST_CASE("cascade splits: endpoints and monotonic", "[cascade]")
{
	const auto s = CascadeMath::ComputeSplits(kNear, 150.0f, 4, 0.8f);
	REQUIRE(s[3] == Approx(150.0f));
	REQUIRE(s[0] > kNear);
	for (uint32_t i = 1; i < 4; ++i)
	{
		REQUIRE(s[i] > s[i - 1]);
	}
}

TEST_CASE("cascade splits: lambda 0 is uniform, lambda 1 is logarithmic", "[cascade]")
{
	const auto uni = CascadeMath::ComputeSplits(1.0f, 101.0f, 4, 0.0f);
	REQUIRE(uni[0] == Approx(26.0f));
	REQUIRE(uni[1] == Approx(51.0f));
	const auto lg = CascadeMath::ComputeSplits(1.0f, 10000.0f, 4, 1.0f);
	REQUIRE(lg[0] == Approx(10.0f));
	REQUIRE(lg[1] == Approx(100.0f));
}

TEST_CASE("cascade splits: a single cascade covers the whole range", "[cascade]")
{
	const auto s = CascadeMath::ComputeSplits(kNear, 80.0f, 1, 0.8f);
	REQUIRE(s[0] == Approx(80.0f));
}

TEST_CASE("slice corners sit at the requested view depths", "[cascade]")
{
	const glm::mat4 vp = CameraViewProj(glm::vec3(0.0f));
	const auto c = CascadeMath::SliceCorners(glm::inverse(vp), kNear, kFar, 10.0f, 40.0f);
	for (int i = 0; i < 4; ++i)
	{
		REQUIRE(-c[i].z == Approx(10.0f).margin(1e-3f));
		REQUIRE(-c[i + 4].z == Approx(40.0f).margin(1e-3f));
	}
}

TEST_CASE("fit sphere contains every slice corner", "[cascade]")
{
	const glm::mat4 vp = CameraViewProj(glm::vec3(3.0f, 2.0f, -5.0f), 0.7f);
	const auto c = CascadeMath::SliceCorners(glm::inverse(vp), kNear, kFar, 5.0f, 60.0f);
	const auto s = CascadeMath::FitSphere(c);
	for (const glm::vec3& p : c)
	{
		REQUIRE(glm::length(p - s.Center) <= s.Radius + 1e-4f);
	}
}

TEST_CASE("fit sphere radius is invariant under camera rotation", "[cascade]")
{
	const auto a = CascadeMath::FitSphere(
	    CascadeMath::SliceCorners(glm::inverse(CameraViewProj(glm::vec3(0.0f), 0.0f)), kNear, kFar, 5.0f, 60.0f));
	const auto b = CascadeMath::FitSphere(
	    CascadeMath::SliceCorners(glm::inverse(CameraViewProj(glm::vec3(0.0f), 1.9f)), kNear, kFar, 5.0f, 60.0f));
	REQUIRE(a.Radius == Approx(b.Radius).margin(0.07f));
}

TEST_CASE("cascade matrix is unchanged by sub-texel translation of the slice centre", "[cascade]")
{
	constexpr uint32_t res = 2048;
	CascadeMath::Sphere s{glm::vec3(10.0f, 4.0f, -3.0f), 25.0f};
	const float texel = CascadeMath::TexelWorldSize(s.Radius, res);

	const glm::mat4 base = CascadeMath::CascadeViewProj(s, kSun, res, 50.0f);
	CascadeMath::Sphere moved = s;
	// Small shift along the light-space X axis: well under half a texel.
	const glm::vec3 right = glm::normalize(glm::cross(kSun, glm::vec3(0, 1, 0)));
	moved.Center += right * (0.2f * texel);
	const glm::mat4 snappedSame = CascadeMath::CascadeViewProj(moved, kSun, res, 50.0f);
	for (int c = 0; c < 4; ++c)
	{
		for (int r = 0; r < 4; ++r)
		{
			REQUIRE(snappedSame[c][r] == Approx(base[c][r]).margin(1e-5f));
		}
	}
}

TEST_CASE("cascade matrix moves in whole texels", "[cascade]")
{
	constexpr uint32_t res = 1024;
	CascadeMath::Sphere s{glm::vec3(1.3f, 0.7f, 2.9f), 12.0f};
	const glm::mat4 vp = CascadeMath::CascadeViewProj(s, kSun, res, 0.0f);
	// The sphere centre projects to within half a texel of NDC origin (snap residual).
	const glm::vec4 ndc = vp * glm::vec4(s.Center, 1.0f);
	const float halfTexelNdc = 1.0f / static_cast<float>(res);
	REQUIRE(std::abs(ndc.x) <= halfTexelNdc + 1e-5f);
	REQUIRE(std::abs(ndc.y) <= halfTexelNdc + 1e-5f);
}

TEST_CASE("every slice corner projects inside the cascade NDC box", "[cascade]")
{
	constexpr uint32_t res = 2048;
	const glm::mat4 vp = CameraViewProj(glm::vec3(-4.0f, 1.5f, 8.0f), 2.3f);
	const auto c = CascadeMath::SliceCorners(glm::inverse(vp), kNear, kFar, 20.0f, 90.0f);
	const auto s = CascadeMath::FitSphere(c);
	const glm::mat4 lvp = CascadeMath::CascadeViewProj(s, kSun, res, 30.0f);
	// Snapping moves the box by up to half a texel, so allow a one-texel margin.
	const float margin = 1.0f + 2.0f / static_cast<float>(res);
	for (const glm::vec3& p : c)
	{
		const glm::vec4 n = lvp * glm::vec4(p, 1.0f);
		REQUIRE(std::abs(n.x) <= margin);
		REQUIRE(std::abs(n.y) <= margin);
		REQUIRE(n.z >= -1e-4f);
		REQUIRE(n.z <= 1.0f + 1e-4f);
	}
}
