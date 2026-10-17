#include <catch2/catch_test_macros.hpp>

#include "Singletons/StandalonePlay.hpp"

using namespace Snowstorm;
namespace fs = std::filesystem;

TEST_CASE("StandalonePlay candidates: beside the editor, then the sibling Runtime target", "[editor][standalone]")
{
	const auto c = StandalonePlay::RuntimeCandidates(fs::path("build") / "Snowstorm-Editor" / "Debug");
	REQUIRE(c.size() == 2);
	// Compared as strings: Catch2 recurses forever stringifying a path, which is itself a range of paths.
	CHECK(c[0].generic_string() == "build/Snowstorm-Editor/Debug/Snowstorm-Runtime.exe");
	CHECK(c[1].generic_string() == "build/Snowstorm-Runtime/Debug/Snowstorm-Runtime.exe");
}

TEST_CASE("StandalonePlay command line quotes exe and scene", "[editor][standalone]")
{
	const auto cmd = StandalonePlay::BuildCommandLine("C:/a b/Snowstorm-Runtime.exe", "C:/c d/s.world");
	CHECK(cmd == L"\"C:/a b/Snowstorm-Runtime.exe\" \"--startup.scene=C:/c d/s.world\"");
}

TEST_CASE("StandalonePlay command line carries the editor's project", "[editor][standalone]")
{
	const auto cmd = StandalonePlay::BuildCommandLine("r.exe", "s.world", "C:/p q/Game.ssproj");
	CHECK(cmd == L"\"r.exe\" \"--startup.scene=s.world\" \"--startup.project=C:/p q/Game.ssproj\"");
}

TEST_CASE("StandalonePlay command line is the full Runtime argument list for a non-Sandbox project", "[editor][standalone]")
{
	const auto cmd = StandalonePlay::BuildCommandLine("C:/My Tools/build/Snowstorm-Runtime.exe",
	                                                  "C:/Users/some one/Snowstorm/Engine/cache/standalone-play.world",
	                                                  "D:/My Games/Space Game/SpaceGame.ssproj");
	CHECK(cmd == L"\"C:/My Tools/build/Snowstorm-Runtime.exe\""
	             L" \"--startup.scene=C:/Users/some one/Snowstorm/Engine/cache/standalone-play.world\""
	             L" \"--startup.project=D:/My Games/Space Game/SpaceGame.ssproj\"");
}

TEST_CASE("StandalonePlay command line omits startup.project without an active project", "[editor][standalone]")
{
	const auto cmd = StandalonePlay::BuildCommandLine("r.exe", "s.world", {});
	CHECK(cmd.find(L"--startup.project") == std::wstring::npos);
}
