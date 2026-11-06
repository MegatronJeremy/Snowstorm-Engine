#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Systems/MeshResolveRule.hpp"
#include "Snowstorm/Systems/ReflectionGeometrySingleton.hpp"
#include "Snowstorm/Systems/ReflectionTablePlan.hpp"

#include <cstddef>

using namespace Snowstorm;

TEST_CASE("MeshResolve: zero handle clears a bound instance and otherwise does nothing", "[mesh][resolve]")
{
	CHECK(DecideMeshResolve(0, true, false, false) == MeshResolveAction::Clear);
	CHECK(DecideMeshResolve(0, false, false, false) == MeshResolveAction::None);
}

TEST_CASE("MeshResolve: an unresolved handle retries and keeps whatever is bound", "[mesh][resolve]")
{
	CHECK(DecideMeshResolve(7, false, false, false) == MeshResolveAction::Retry);
	CHECK(DecideMeshResolve(7, true, false, false) == MeshResolveAction::Retry);
}

TEST_CASE("MeshResolve: a resident mesh is assigned once, then left alone", "[mesh][resolve]")
{
	CHECK(DecideMeshResolve(7, false, true, false) == MeshResolveAction::Assign);
	CHECK(DecideMeshResolve(7, true, true, false) == MeshResolveAction::Assign); // handle retargeted
	CHECK(DecideMeshResolve(7, true, true, true) == MeshResolveAction::None);
}

TEST_CASE("MeshResolve: safety net fires exactly when handle and instance disagree", "[mesh][resolve]")
{
	CHECK(MeshNeedsSafetyNetResolve(7, false));
	CHECK(MeshNeedsSafetyNetResolve(0, true));
	CHECK_FALSE(MeshNeedsSafetyNetResolve(7, true));
	CHECK_FALSE(MeshNeedsSafetyNetResolve(0, false));
}

TEST_CASE("MeshResolve: a pending load is retried every frame until it lands", "[mesh][resolve]")
{
	bool instance = false;
	for (int frame = 0; frame < 3; ++frame)
	{
		REQUIRE(MeshNeedsSafetyNetResolve(5, instance));
		REQUIRE(DecideMeshResolve(5, instance, false, false) == MeshResolveAction::Retry);
	}
	REQUIRE(DecideMeshResolve(5, instance, true, false) == MeshResolveAction::Assign);
	instance = true;
	CHECK_FALSE(MeshNeedsSafetyNetResolve(5, instance));
}

TEST_CASE("ReflectionTablePlan: first non-empty gather creates and publishes", "[reflection][tlas]")
{
	const auto plan = PlanReflectionTable(10, false, 0);
	CHECK(plan.Publish);
	CHECK(plan.Recreate);
	CHECK(plan.NewCapacity == 10);
}

TEST_CASE("ReflectionTablePlan: a fitting gather reuses the buffer but still publishes", "[reflection][tlas]")
{
	const auto plan = PlanReflectionTable(8, true, 10);
	CHECK(plan.Publish);
	CHECK_FALSE(plan.Recreate);
	CHECK(plan.NewCapacity == 10);

	const auto exact = PlanReflectionTable(10, true, 10);
	CHECK_FALSE(exact.Recreate);
}

TEST_CASE("ReflectionTablePlan: outgrowing the capacity recreates at the new count", "[reflection][tlas]")
{
	const auto plan = PlanReflectionTable(11, true, 10);
	CHECK(plan.Publish);
	CHECK(plan.Recreate);
	CHECK(plan.NewCapacity == 11);
}

TEST_CASE("ReflectionTablePlan: an empty gather publishes nothing and keeps the cached capacity", "[reflection][tlas]")
{
	const auto plan = PlanReflectionTable(0, true, 10);
	CHECK_FALSE(plan.Publish);
	CHECK_FALSE(plan.Recreate);
	CHECK(plan.NewCapacity == 10);

	// RT off then on again: the cached buffer is reused.
	CHECK_FALSE(PlanReflectionTable(4, true, plan.NewCapacity).Recreate);
}

TEST_CASE("ReflectionGeometrySingleton: starts with no table and address 0", "[reflection][tlas]")
{
	const ReflectionGeometrySingleton s;
	CHECK(s.Table == nullptr);
	CHECK(s.Capacity == 0);
	CHECK(s.TableAddress == 0);
}

TEST_CASE("GeometryRecord: byte offsets match the HLSL GeoRecord contract", "[reflection][tlas]")
{
	CHECK(offsetof(GeometryRecord, IndexAddress) == 8);
	CHECK(offsetof(GeometryRecord, AlbedoTextureIndex) == 16);
	CHECK(offsetof(GeometryRecord, AlphaCutoff) == 24);
	CHECK(offsetof(GeometryRecord, BaseColor) == 32);
	CHECK(offsetof(GeometryRecord, Model) == 48);
	CHECK(offsetof(GeometryRecord, MetallicRoughnessTextureIndex) == 112);
	CHECK(offsetof(GeometryRecord, Metallic) == 124);
	CHECK(offsetof(GeometryRecord, EmissiveB) == 140);
}
