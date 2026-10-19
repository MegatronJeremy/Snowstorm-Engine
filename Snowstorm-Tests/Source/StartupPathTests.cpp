#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Utility/EnginePaths.hpp"

#include <filesystem>
#include <fstream>

using namespace Snowstorm;
namespace fs = std::filesystem;

// A double-clicked exe runs with its own folder as the working directory, so a repo-relative startup path
// must not be judged against that folder alone. The engine-root branch needs the real tree and is covered by
// the smoke gate's --from-exe-dir; these pin the cases that must pass through unchanged.

TEST_CASE("ResolveStartupPath leaves an absolute path alone", "[startuppath]")
{
	const fs::path absolute = fs::temp_directory_path() / "no-such-project" / "X.ssproj";
	CHECK(ResolveStartupPath(absolute) == absolute);
}

TEST_CASE("ResolveStartupPath leaves a path that exists from the working directory alone", "[startuppath]")
{
	const fs::path here = fs::current_path();
	const fs::path relative = fs::relative(here / "StartupPathTests.tmp", here);
	const fs::path file = here / relative;
	{
		std::error_code ec;
		fs::create_directories(file.parent_path(), ec);
		std::ofstream(file) << "x";
	}
	CHECK(ResolveStartupPath(relative) == relative);
	fs::remove(file);
}

TEST_CASE("ResolveStartupPath returns an unresolvable path unchanged so the error names what was typed", "[startuppath]")
{
	const fs::path missing = "Projects/DoesNotExist/DoesNotExist.ssproj";
	CHECK(ResolveStartupPath(missing) == missing);
	CHECK(ResolveStartupPath(fs::path{}).empty());
}
