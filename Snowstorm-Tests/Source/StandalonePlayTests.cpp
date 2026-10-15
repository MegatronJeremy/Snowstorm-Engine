#include <catch2/catch_test_macros.hpp>

#include "Singletons/StandalonePlay.hpp"

#include <fstream>
#include <sstream>

using namespace Snowstorm;
namespace fs = std::filesystem;

TEST_CASE("StandalonePlay candidates: beside the editor, then the sibling Runtime target", "[editor][standalone]")
{
	const auto c = StandalonePlay::RuntimeCandidates(fs::path("build") / "Snowstorm-Editor" / "Debug");
	REQUIRE(c.size() == 2);
	CHECK(c[0] == fs::path("build") / "Snowstorm-Editor" / "Debug" / "Snowstorm-Runtime.exe");
	CHECK(c[1] == fs::path("build") / "Snowstorm-Runtime" / "Debug" / "Snowstorm-Runtime.exe");
}

TEST_CASE("StandalonePlay command line quotes exe and scene", "[editor][standalone]")
{
	const auto cmd = StandalonePlay::BuildCommandLine("C:/a b/Snowstorm-Runtime.exe", "C:/c d/s.world");
	CHECK(cmd == L"\"C:/a b/Snowstorm-Runtime.exe\" \"--startup.scene=C:/c d/s.world\"");
}

TEST_CASE("StandalonePlay writes the scene verbatim", "[editor][standalone]")
{
	const fs::path dir = fs::temp_directory_path() / "ss-standalone-play-test";
	const auto path = StandalonePlay::WriteTempScene(dir, "{\"a\":1}");
	REQUIRE(!path.empty());
	std::ifstream in(path, std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	CHECK(ss.str() == "{\"a\":1}");
	fs::remove_all(dir);
}
