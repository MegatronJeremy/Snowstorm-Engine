#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Utility/CVar.hpp"

using namespace Snowstorm;

// The live CVar panel edits values through the typed ICVar accessors (GetKind + Get/Set Bool/Int/Float),
// dispatching on GetKind(). These guard that contract: the kind is right and the matching accessor
// round-trips, so the panel's widgets actually mutate the underlying value.

TEST_CASE("CVar reports the correct kind", "[cvar]")
{
	const CVar<bool> b{"test.bool", false, ""};
	const CVar<int> i{"test.int", 0, ""};
	const CVar<float> f{"test.float", 0.0f, ""};
	const CVar<std::string> s{"test.string", "", ""};

	REQUIRE(static_cast<const ICVar&>(b).GetKind() == CVarKind::Bool);
	REQUIRE(static_cast<const ICVar&>(i).GetKind() == CVarKind::Int);
	REQUIRE(static_cast<const ICVar&>(f).GetKind() == CVarKind::Float);
	REQUIRE(static_cast<const ICVar&>(s).GetKind() == CVarKind::String);
}

TEST_CASE("CVar typed set/get round-trips through the ICVar interface", "[cvar]")
{
	CVar<bool> b{"test.bool2", false, ""};
	ICVar& bi = b;
	bi.SetBool(true);
	REQUIRE(bi.GetBool());
	REQUIRE(b.Get() == true); // the typed setter mutates the real value the engine reads

	CVar<int> i{"test.int2", 1, ""};
	ICVar& ii = i;
	ii.SetInt(42);
	REQUIRE(ii.GetInt() == 42);
	REQUIRE(i.Get() == 42);

	CVar<float> f{"test.float2", 1.0f, ""};
	ICVar& fi = f;
	fi.SetFloat(2.5f);
	REQUIRE(fi.GetFloat() == 2.5f);
	REQUIRE(f.Get() == 2.5f);
}

TEST_CASE("CVar string command path still sets any type", "[cvar]")
{
	CVar<int> i{"test.int3", 0, ""};
	static_cast<ICVar&>(i).SetFromString("7");
	REQUIRE(i.Get() == 7);

	CVar<bool> b{"test.bool3", false, ""};
	static_cast<ICVar&>(b).SetFromString("true");
	REQUIRE(b.Get() == true);
}

// Default tracking backs two features: SaveConfig persists only changed CVars (so a code-default change
// propagates instead of being shadowed by a stale persisted copy), and the editor's "Reset to Defaults".
TEST_CASE("CVar tracks its default: IsAtDefault + ResetToDefault", "[cvar]")
{
	CVar<float> f{"test.default.float", 0.9f, ""};
	ICVar& fi = f;
	REQUIRE(fi.IsAtDefault()); // constructed value == default
	REQUIRE(f.GetDefault() == 0.9f);

	f.Set(0.75f);
	REQUIRE_FALSE(fi.IsAtDefault()); // changed -> not at default (this is the SaveConfig skip predicate)

	fi.ResetToDefault();
	REQUIRE(fi.IsAtDefault());
	REQUIRE(f.Get() == 0.9f); // back to the code default

	// A value re-set to exactly the default reads as at-default again (bit-identical), so it won't persist.
	f.Set(0.5f);
	REQUIRE_FALSE(fi.IsAtDefault());
	f.Set(0.9f);
	REQUIRE(fi.IsAtDefault());
}
