#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include "Snowstorm/Render/EmissiveLightList.hpp"

using namespace Snowstorm;
using namespace Snowstorm::EmissiveLightList;
using Catch::Approx;

namespace
{
	// Two triangles: areas 2 and 8 in the XY plane.
	const std::vector<glm::vec3> kPositions = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}, {0, 0, 0}, {4, 0, 0}, {0, 4, 0}};
	const std::vector<uint32_t> kIndices = {0, 1, 2, 3, 4, 5};

	InstanceInput Make(uint32_t instance, glm::vec3 emissive, glm::mat4 model = glm::mat4(1.0f))
	{
		InstanceInput in;
		in.Instance = instance;
		in.Model = model;
		in.Emissive = emissive;
		in.Positions = kPositions.data();
		in.PositionCount = kPositions.size();
		in.Indices = kIndices.data();
		in.IndexCount = kIndices.size();
		return in;
	}
}

TEST_CASE("emissive list: layout matches the shader's 64 byte records", "[emissive]")
{
	CHECK(sizeof(GpuTriangle) == 64);
	CHECK(offsetof(GpuTriangle, Instance) == 12);
	CHECK(offsetof(GpuTriangle, P1) == 16);
	CHECK(offsetof(GpuTriangle, Prim) == 28);
	CHECK(offsetof(GpuTriangle, P2) == 32);
	CHECK(offsetof(GpuTriangle, Cdf) == 44);
	CHECK(offsetof(GpuTriangle, Pmf) == 48);
	CHECK(offsetof(GpuTriangle, Area) == 52);
}

TEST_CASE("emissive list: weight is world area times emissive luminance", "[emissive]")
{
	const Result r = Build({Make(7, glm::vec3(1.0f))});
	REQUIRE(r.Triangles.size() == 2);
	CHECK(r.FirstTriangle[0] == 1);
	CHECK(r.Triangles[0].Instance == 7);
	CHECK(r.Triangles[1].Prim == 1);
	CHECK(r.Triangles[0].Area == Approx(2.0f));
	CHECK(r.Triangles[1].Area == Approx(8.0f));
	CHECK(r.Triangles[0].Pmf == Approx(0.2f));
	CHECK(r.Triangles[1].Pmf == Approx(0.8f));
	CHECK(r.Triangles[0].Cdf == Approx(0.2f));
	CHECK(r.Triangles[1].Cdf == 1.0f);
}

TEST_CASE("emissive list: the model matrix scales area and positions", "[emissive]")
{
	const Result r = Build({Make(0, glm::vec3(1.0f), glm::scale(glm::mat4(1.0f), glm::vec3(3.0f)))});
	REQUIRE(r.Triangles.size() == 2);
	CHECK(r.Triangles[0].Area == Approx(18.0f));
	CHECK(r.Triangles[0].P1[0] == Approx(6.0f));
}

TEST_CASE("emissive list: non-emissive instances are skipped and offsets index across instances", "[emissive]")
{
	const Result r = Build({Make(0, glm::vec3(0.0f)), Make(1, glm::vec3(1.0f)), Make(2, glm::vec3(2.0f))});
	REQUIRE(r.Triangles.size() == 4);
	CHECK(r.FirstTriangle[0] == 0);
	CHECK(r.FirstTriangle[1] == 1);
	CHECK(r.FirstTriangle[2] == 3);
	// Entry of (instance 2, prim 1) is FirstTriangle - 1 + prim.
	const GpuTriangle& t = r.Triangles[r.FirstTriangle[2] - 1 + 1];
	CHECK(t.Instance == 2);
	CHECK(t.Prim == 1);
	// Instance 2 is twice as bright, so it carries 2/3 of the power.
	CHECK(r.Triangles[1].Cdf == Approx(1.0f / 3.0f));
}

TEST_CASE("emissive list: all-black or degenerate input yields an empty list", "[emissive]")
{
	CHECK(Build({Make(0, glm::vec3(0.0f))}).Triangles.empty());
	CHECK(Build({}).Triangles.empty());
	const std::vector<glm::vec3> flat = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
	const std::vector<uint32_t> idx = {0, 1, 2};
	InstanceInput in;
	in.Emissive = glm::vec3(1.0f);
	in.Positions = flat.data();
	in.PositionCount = flat.size();
	in.Indices = idx.data();
	in.IndexCount = idx.size();
	const Result r = Build({in});
	CHECK(r.Triangles.empty());
	CHECK(r.FirstTriangle[0] == 0);
}

TEST_CASE("emissive list: an out-of-range index gets zero weight instead of reading past the positions", "[emissive]")
{
	std::vector<uint32_t> idx = {0, 1, 2, 0, 1, 99};
	InstanceInput in = Make(0, glm::vec3(1.0f));
	in.Indices = idx.data();
	const Result r = Build({in});
	REQUIRE(r.Triangles.size() == 2);
	CHECK(r.Triangles[1].Pmf == 0.0f);
	CHECK(r.Triangles[0].Pmf == Approx(1.0f));
}
