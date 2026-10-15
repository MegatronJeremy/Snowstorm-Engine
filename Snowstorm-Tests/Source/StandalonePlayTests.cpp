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
