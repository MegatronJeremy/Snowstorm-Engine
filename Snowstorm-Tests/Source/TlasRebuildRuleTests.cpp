#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Systems/TlasRebuildRule.hpp"

using namespace Snowstorm::TlasRebuildRule;

namespace
{
	RebuildState Steady()
	{
		RebuildState s;
		s.RtActive = true;
		s.WasRtActive = true;
		s.BuiltOnce = true;
		return s;
	}
}

TEST_CASE("TlasRebuildRule: scene dirtiness", "[tlas]")
{
	SECTION("no signals is clean")
	{
		REQUIRE_FALSE(IsSceneDirty({}));
	}
	SECTION("a transform change on a non-mesh entity (the camera) is clean")
	{
		SceneChanges c; // MeshTransformChanged stays false: the system filters non-mesh transforms
		REQUIRE_FALSE(IsSceneDirty(c));
	}
	SECTION("each single signal dirties the scene")
	{
		SceneChanges c;
		c.MeshChanged = true;
		REQUIRE(IsSceneDirty(c));
		c = {};
		c.MeshOrTransformInit = true;
		REQUIRE(IsSceneDirty(c));
		c = {};
		c.MeshOrTransformFini = true;
		REQUIRE(IsSceneDirty(c));
		c = {};
		c.AnyDestroyed = true;
		REQUIRE(IsSceneDirty(c));
		c = {};
		c.MaterialChanged = true;
		REQUIRE(IsSceneDirty(c));
		c = {};
		c.MeshTransformChanged = true;
		REQUIRE(IsSceneDirty(c));
	}
}

TEST_CASE("TlasRebuildRule: rebuild gate", "[tlas]")
{
	SECTION("steady state with a clean scene does not rebuild")
	{
		REQUIRE_FALSE(ShouldRebuild(Steady(), false));
	}
	SECTION("a dirty scene rebuilds")
	{
		REQUIRE(ShouldRebuild(Steady(), true));
	}
	SECTION("the first build always happens")
	{
		RebuildState s = Steady();
		s.BuiltOnce = false;
		REQUIRE(ShouldRebuild(s, false));
	}
	SECTION("RT off to on forces a rebuild on that frame only")
	{
		RebuildState s = Steady();
		s.WasRtActive = false;
		REQUIRE(JustEnabled(s));
		REQUIRE(ShouldRebuild(s, false));
		s.WasRtActive = true;
		REQUIRE_FALSE(JustEnabled(s));
		REQUIRE_FALSE(ShouldRebuild(s, false));
	}
	SECTION("an OMM toggle forces a rebuild in either direction")
	{
		RebuildState s = Steady();
		s.OmmEnabled = false;
		s.LastOmmEnabled = true;
		REQUIRE(ShouldRebuild(s, false));
		s.OmmEnabled = true;
		s.LastOmmEnabled = false;
		REQUIRE(ShouldRebuild(s, false));
	}
	SECTION("an OMM edge before the first build is not a toggle")
	{
		RebuildState s = Steady();
		s.BuiltOnce = false;
		s.OmmEnabled = false;
		REQUIRE_FALSE(OmmToggled(s));
	}
	SECTION("pending loads keep rebuilding")
	{
		RebuildState s = Steady();
		s.PendingLoads = 3;
		s.PrevPendingLoads = 3;
		REQUIRE(ShouldRebuild(s, false));
	}
	SECTION("streaming lasts exactly one frame past the drain")
	{
		RebuildState s = Steady();
		s.PendingLoads = 0;
		s.PrevPendingLoads = 2; // the drain frame
		REQUIRE(Streaming(s));
		REQUIRE(ShouldRebuild(s, false));
		s.PrevPendingLoads = 0; // the frame after
		REQUIRE_FALSE(Streaming(s));
		REQUIRE_FALSE(ShouldRebuild(s, false));
	}
}
