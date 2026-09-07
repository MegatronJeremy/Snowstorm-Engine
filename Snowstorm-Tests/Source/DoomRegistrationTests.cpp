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

// DoomComponent lives in the Doom GAME library, not the engine, and its registration is a static
// initializer in a translation unit nothing references. Only a WHOLE_ARCHIVE link keeps it, and losing
// it is silent in every direction: SceneSerializer skips an unregistered component with a bare
// `continue`, so Doom.world would still load, the quad would just never show Doom, and a subsequent
// save would delete the block from the scene file.
//
// The name is unqualified ("DoomComponent", not "Snowstorm::DoomComponent") because that is the string
// DoomComponent.cpp registers and therefore the serialization key already committed in Doom.world.
// Asserting the exact string here is deliberate: changing it silently orphans the scene data.
//
// This holds in the DEFAULT build too. The game library is built whether or not SS_ENABLE_DOOM is set,
// so a stock editor still round-trips Doom.world; only the GPL doomgeneric link is gated.
TEST_CASE("Doom game components survive the link into a host", "[doom]")
{
	CHECK(IsComponentRegistered("DoomComponent"));
}

TEST_CASE("Engine components are still registered alongside a linked game", "[doom]")
{
	// Guards against the assertion above passing for the wrong reason, i.e. a registry that matches
	// everything or a predicate that is trivially true.
	CHECK(IsComponentRegistered("Snowstorm::TransformComponent"));
	CHECK_FALSE(IsComponentRegistered("NoSuchComponent"));
}
