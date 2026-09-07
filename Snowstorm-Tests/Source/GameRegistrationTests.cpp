#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/ComponentRegistry.hpp"

#include <algorithm>
#include <string>

using namespace Snowstorm;

namespace
{
	bool IsComponentRegistered(const std::string& name)
	{
		const auto& registry = GetComponentRegistry();
		return std::ranges::any_of(registry,
		                           [&](const ComponentInfo& info)
		                           {
			                           return info.Type.is_valid() && info.Type.get_name().to_string() == name;
		                           });
	}
}

// A game's components live in its own library, and their registration is a static initializer in a
// translation unit nothing references. Only a WHOLE_ARCHIVE link keeps it, and losing it is silent in
// every direction: SceneSerializer skips an unregistered component with a bare `continue`, so the scene
// still loads and the entity simply does nothing, and a subsequent save deletes the block from the file.
//
// The names are unqualified because that is what PongComponents.cpp registers, and therefore the
// serialization key already committed in Pong.world. Asserting the exact strings is deliberate: changing
// one silently orphans the scene data.
TEST_CASE("Example game components survive the link into a host", "[games]")
{
	CHECK(IsComponentRegistered("PongPaddleComponent"));
	CHECK(IsComponentRegistered("PongBallComponent"));
}

TEST_CASE("Engine components are still registered alongside a linked game", "[games]")
{
	// Guards against the assertion above passing for the wrong reason, i.e. a registry that matches
	// everything or a predicate that is trivially true.
	CHECK(IsComponentRegistered("Snowstorm::TransformComponent"));
	CHECK_FALSE(IsComponentRegistered("NoSuchComponent"));
}
