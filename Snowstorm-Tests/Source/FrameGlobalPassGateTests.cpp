#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "Snowstorm/Render/FrameGlobalPassGate.hpp"

using namespace Snowstorm;
using Path = CameraComponent::RenderPathType;

TEST_CASE("Frame-global scene passes need a rendering Scene3D viewport", "[render][gate]")
{
	CHECK_FALSE(NeedsSceneGlobalPasses({}));
	CHECK_FALSE(NeedsSceneGlobalPasses(std::vector<Path>{Path::Canvas2D}));
	CHECK_FALSE(NeedsSceneGlobalPasses(std::vector<Path>{Path::Canvas2D, Path::Canvas2D}));
	CHECK(NeedsSceneGlobalPasses(std::vector<Path>{Path::Scene3D}));
	CHECK(NeedsSceneGlobalPasses(std::vector<Path>{Path::Canvas2D, Path::Scene3D}));
}
