#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Core/GameCameraRule.hpp"

using namespace Snowstorm;

TEST_CASE("DecideGameCamera: no Primary camera plans nothing", "[camera]")
{
	const GameCameraPlan plan = DecideGameCamera(false, false, "");
	CHECK_FALSE(plan.HasCamera);
	CHECK_FALSE(plan.AddTransform);
	CHECK_FALSE(plan.ApplyOverride);
}

TEST_CASE("DecideGameCamera: camera.override is ignored without a Primary camera", "[camera]")
{
	const GameCameraPlan plan = DecideGameCamera(false, false, "1,2,3,0,0,0");
	CHECK_FALSE(plan.HasCamera);
	CHECK_FALSE(plan.ApplyOverride);
}

TEST_CASE("DecideGameCamera: a Primary camera with a Transform is used as authored", "[camera]")
{
	const GameCameraPlan plan = DecideGameCamera(true, true, "");
	CHECK(plan.HasCamera);
	CHECK_FALSE(plan.AddTransform);
	CHECK_FALSE(plan.ApplyOverride);
}

TEST_CASE("DecideGameCamera: a bare camera gets a Transform", "[camera]")
{
	const GameCameraPlan plan = DecideGameCamera(true, false, "");
	CHECK(plan.HasCamera);
	CHECK(plan.AddTransform);
}

TEST_CASE("DecideGameCamera: camera.override outranks the authored pose", "[camera]")
{
	const GameCameraPlan plan = DecideGameCamera(true, true, "1,2,3,0.5,0.25,-1");
	CHECK(plan.ApplyOverride);
	CHECK(plan.OverridePosition == glm::vec3(1.0f, 2.0f, 3.0f));
	CHECK(plan.OverrideRotation == glm::vec3(0.5f, 0.25f, -1.0f));
}

TEST_CASE("DecideGameCamera: a malformed camera.override leaves the authored pose", "[camera]")
{
	CHECK_FALSE(DecideGameCamera(true, true, "1,2,3").ApplyOverride);
	CHECK_FALSE(DecideGameCamera(true, true, "not,a,pose,at,all,x").ApplyOverride);
}
