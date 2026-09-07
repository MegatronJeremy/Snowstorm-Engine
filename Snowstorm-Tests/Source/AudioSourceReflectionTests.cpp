#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/AudioSourceComponent.hpp"

#include <rttr/type>

#include <string>

using namespace Snowstorm;

namespace
{
	bool IsReflected(const std::string& name)
	{
		const rttr::type type = rttr::type::get<AudioSourceComponent>();
		REQUIRE(type.is_valid());
		return type.get_property(name).is_valid();
	}
}

// AudioSourceComponent's RTTR property list is simultaneously the serialization set and the inspector
// set, so a property's presence there is the only thing deciding whether it reaches the .world file.
// PlayRequested and StopRequested are one-frame runtime requests: registering either (for instance to
// get an inspector checkbox) would write it into the scene and re-trigger the sound on every load.
//
// Tested rather than left to the header comment because the failure is silent, survives a build, and
// only shows up as a sound that plays itself when a scene is opened.
TEST_CASE("AudioSourceComponent runtime requests are not reflected, so they never serialize", "[audio]")
{
	CHECK_FALSE(IsReflected("PlayRequested"));
	CHECK_FALSE(IsReflected("StopRequested"));
}

TEST_CASE("AudioSourceComponent authored fields stay reflected", "[audio]")
{
	// The other half of the invariant: the guard above must not be satisfiable by the registration
	// disappearing wholesale.
	CHECK(IsReflected("Clip"));
	CHECK(IsReflected("Volume"));
	CHECK(IsReflected("Pitch"));
	CHECK(IsReflected("Loop"));
	CHECK(IsReflected("PlayOnStart"));
	CHECK(IsReflected("Spatial"));
	CHECK(IsReflected("MinDistance"));
	CHECK(IsReflected("MaxDistance"));
}
