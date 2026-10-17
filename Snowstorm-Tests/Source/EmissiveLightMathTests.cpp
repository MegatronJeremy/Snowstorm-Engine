#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/EmissiveLightMath.hpp"

using namespace Snowstorm;
using namespace Snowstorm::EmissiveLightMath;
using Catch::Approx;

namespace
{
	const glm::vec3 kP0(0.0f, 0.0f, 0.0f);
	const glm::vec3 kP1(4.0f, 0.0f, 0.0f);
	const glm::vec3 kP2(0.0f, 3.0f, 0.0f);
}

TEST_CASE("emissive light: triangle area", "[emissive]")
{
	CHECK(TriangleArea(kP0, kP1, kP2) == Approx(6.0f));
	CHECK(TriangleArea(kP0, kP0, kP2) == Approx(0.0f));
}

TEST_CASE("emissive light: triangle samples stay inside and barycentrics sum to 1", "[emissive]")
{
	constexpr int n = 32;
	for (int i = 0; i < n; ++i)
	{
		for (int j = 0; j < n; ++j)
		{
			const TriangleSample s = SampleTriangleUniform(kP0, kP1, kP2, (i + 0.5f) / n, (j + 0.5f) / n);
			CHECK(s.Barycentric.x >= 0.0f);
			CHECK(s.Barycentric.y >= 0.0f);
			CHECK(s.Barycentric.z >= -1e-6f);
			CHECK(s.Barycentric.x + s.Barycentric.y + s.Barycentric.z == Approx(1.0f));
			CHECK(s.Position.z == Approx(0.0f));
		}
	}
}

TEST_CASE("emissive light: stratified samples average to the centroid (area-preserving map)", "[emissive]")
{
	constexpr int n = 64;
	glm::dvec3 sum(0.0);
	for (int i = 0; i < n; ++i)
	{
		for (int j = 0; j < n; ++j)
		{
			sum += glm::dvec3(SampleTriangleUniform(kP0, kP1, kP2, (i + 0.5f) / n, (j + 0.5f) / n).Position);
		}
	}
	const glm::dvec3 mean = sum / double(n * n);
	CHECK(mean.x == Approx(4.0 / 3.0).margin(0.02));
	CHECK(mean.y == Approx(1.0).margin(0.02));
}

TEST_CASE("emissive light: area pdf converts to solid angle", "[emissive]")
{
	// A unit-area light at distance 2 seen head-on: 1 * 4 / 1.
	CHECK(AreaPdfToSolidAngle(1.0f, 4.0f, 1.0f) == Approx(4.0f));
	// Foreshortening at 60 degrees doubles the density; the back face gives the same as the front.
	CHECK(AreaPdfToSolidAngle(1.0f, 4.0f, 0.5f) == Approx(8.0f));
	CHECK(AreaPdfToSolidAngle(1.0f, 4.0f, -0.5f) == Approx(8.0f));
	CHECK(AreaPdfToSolidAngle(1.0f, 4.0f, 0.0f) == 0.0f);
}

TEST_CASE("emissive light: balance weights of the two strategies partition 1", "[emissive]")
{
	CHECK(BalanceWeight(3.0f, 1.0f) + BalanceWeight(1.0f, 3.0f) == Approx(1.0f));
	CHECK(BalanceWeight(2.0f, 2.0f) == Approx(0.5f));
	CHECK(BalanceWeight(0.0f, 5.0f) == 0.0f);
	CHECK(BalanceWeight(0.0f, 0.0f) == 0.0f);
}

TEST_CASE("emissive light: CDF selection matches weights and skips zero buckets", "[emissive]")
{
	CHECK(BuildCdf({0.0f, 0.0f}).empty());
	CHECK(BuildCdf({}).empty());

	const std::vector<float> cdf = BuildCdf({1.0f, 0.0f, 3.0f});
	REQUIRE(cdf.size() == 3);
	CHECK(cdf.back() == 1.0f);

	const CdfPick a = PickFromCdf(cdf, 0.1f);
	CHECK(a.Index == 0);
	CHECK(a.Pmf == Approx(0.25f));
	CHECK(a.URemapped == Approx(0.4f));

	const CdfPick b = PickFromCdf(cdf, 0.25f); // boundary belongs to the next non-empty bucket
	CHECK(b.Index == 2);
	CHECK(b.Pmf == Approx(0.75f));
	CHECK(b.URemapped == Approx(0.0f).margin(1e-6));

	const CdfPick c = PickFromCdf(cdf, 0.99999994f);
	CHECK(c.Index == 2);
	CHECK(c.URemapped < 1.0f);

	// Each index is picked with frequency equal to its pmf over a uniform grid of u.
	uint32_t hits[3] = {0, 0, 0};
	constexpr int n = 4000;
	for (int i = 0; i < n; ++i)
	{
		++hits[PickFromCdf(cdf, (i + 0.5f) / n).Index];
	}
	CHECK(hits[0] / float(n) == Approx(0.25f).margin(0.002));
	CHECK(hits[1] == 0);
	CHECK(hits[2] / float(n) == Approx(0.75f).margin(0.002));
}
