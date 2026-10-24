#include <catch2/catch_test_macros.hpp>

#include "System/ConsoleCommand.hpp"

#include <unordered_map>

using namespace Snowstorm;

// CVars register with the process-global registry and never unregister, so these are static. The lookup is a
// local table, so the parse is tested without depending on the real engine CVars.
namespace
{
	CVar<bool> g_Flag{"test.consolecmd.flag", false, ""};
	CVar<int> g_Count{"test.consolecmd.count", 3, ""};
	CVar<float> g_Scale{"test.consolecmd.scale", 1.0f, ""};
	CVar<std::string> g_Path{"test.consolecmd.path", "a", ""};
	CVar<int> g_Locked{"test.consolecmd.locked", 7, "", CVarFlags::ReadOnly};

	ConsoleCommand Parse(const std::string& line)
	{
		const std::unordered_map<std::string, ICVar*> table = {{"test.consolecmd.flag", &g_Flag},
		                                                       {"test.consolecmd.count", &g_Count},
		                                                       {"test.consolecmd.scale", &g_Scale},
		                                                       {"test.consolecmd.path", &g_Path},
		                                                       {"test.consolecmd.locked", &g_Locked}};
		return ParseConsoleCommand(line,
		                           [&](const std::string& name)
		                           {
			                           const auto it = table.find(name);
			                           return it == table.end() ? nullptr : it->second;
		                           });
	}
}

TEST_CASE("Console: blank lines are empty", "[console]")
{
	REQUIRE(Parse("").Kind == ConsoleCommandKind::Empty);
	REQUIRE(Parse("   \t ").Kind == ConsoleCommandKind::Empty);
}

TEST_CASE("Console: trims spaces and tabs at both ends", "[console]")
{
	const ConsoleCommand c = Parse(" \t test.consolecmd.count 5 \t ");
	REQUIRE(c.Trimmed == "test.consolecmd.count 5");
	REQUIRE(c.Verb == "test.consolecmd.count");
	REQUIRE(c.Rest == "5");
	REQUIRE(c.Kind == ConsoleCommandKind::SetCVar);
}

TEST_CASE("Console: built-ins", "[console]")
{
	REQUIRE(Parse("help").Kind == ConsoleCommandKind::Help);
	REQUIRE(Parse("clear").Kind == ConsoleCommandKind::Clear);
	const ConsoleCommand list = Parse("list render");
	REQUIRE(list.Kind == ConsoleCommandKind::List);
	REQUIRE(list.Rest == "render");
	REQUIRE(Parse("list").Rest.empty());
}

TEST_CASE("Console: a bare name shows the value, a bare unknown name warns", "[console]")
{
	REQUIRE(Parse("test.consolecmd.flag").Kind == ConsoleCommandKind::ShowCVar);
	REQUIRE(Parse("nope").Kind == ConsoleCommandKind::UnknownName);
}

TEST_CASE("Console: unknown name with a value is UnknownCVar", "[console]")
{
	const ConsoleCommand c = Parse("nope 1");
	REQUIRE(c.Kind == ConsoleCommandKind::UnknownCVar);
	REQUIRE(c.Verb == "nope");
	REQUIRE(c.CVar == nullptr);
}

TEST_CASE("Console: a ReadOnly CVar is rejected, not set", "[console]")
{
	const ConsoleCommand c = Parse("test.consolecmd.locked 9");
	REQUIRE(c.Kind == ConsoleCommandKind::SetReadOnly);
	REQUIRE(g_Locked.Get() == 7);
}

TEST_CASE("Console: a built-in verb wins over a value-less lookup", "[console]")
{
	// "help me" is still help; the rest is carried but unused.
	const ConsoleCommand c = Parse("help me");
	REQUIRE(c.Kind == ConsoleCommandKind::Help);
	REQUIRE(c.Rest == "me");
}

TEST_CASE("Console: value coercion per CVar type", "[console]")
{
	ConsoleCommand c = Parse("test.consolecmd.flag off");
	REQUIRE(c.Kind == ConsoleCommandKind::SetCVar);
	g_Flag.Set(true);
	c.CVar->SetFromString(c.Rest);
	REQUIRE_FALSE(g_Flag.Get());
	Parse("test.consolecmd.flag TRUE").CVar->SetFromString("TRUE");
	REQUIRE(g_Flag.Get());
	for (const char* off : {"0", "false", "No", "OFF"})
	{
		c.CVar->SetFromString(off);
		REQUIRE_FALSE(g_Flag.Get());
	}
	c.CVar->SetFromString("anything");
	REQUIRE(g_Flag.Get());

	c = Parse("test.consolecmd.count 42");
	c.CVar->SetFromString(c.Rest);
	REQUIRE(g_Count.Get() == 42);
	c.CVar->SetFromString("-5");
	REQUIRE(g_Count.Get() == -5);

	c = Parse("test.consolecmd.scale 0.25");
	c.CVar->SetFromString(c.Rest);
	REQUIRE(g_Scale.Get() == 0.25f);

	c = Parse("test.consolecmd.path some dir/x y");
	REQUIRE(c.Rest == "some dir/x y");
	c.CVar->SetFromString(c.Rest);
	REQUIRE(g_Path.Get() == "some dir/x y");
}

TEST_CASE("Console: bad numeric input keeps the previous value", "[console]")
{
	g_Count.Set(11);
	g_Scale.Set(2.0f);
	Parse("test.consolecmd.count abc").CVar->SetFromString("abc");
	Parse("test.consolecmd.scale xyz").CVar->SetFromString("xyz");
	REQUIRE(g_Count.Get() == 11);
	REQUIRE(g_Scale.Get() == 2.0f);
}
