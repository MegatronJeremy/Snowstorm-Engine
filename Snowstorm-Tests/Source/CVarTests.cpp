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
