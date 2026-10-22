#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Utility/EnginePaths.hpp"
#include "Snowstorm/Utility/SettingsPaths.hpp"

#include <filesystem>
#include <fstream>

using namespace Snowstorm;
namespace fs = std::filesystem;

namespace
{
	struct TempTree
	{
		fs::path Root;

		TempTree()
		    : Root(fs::temp_directory_path() / ("ss_enginepaths_" + std::to_string(reinterpret_cast<uintptr_t>(this))))
		{
			fs::remove_all(Root);
			fs::create_directories(Root);
		}

		~TempTree()
		{
			std::error_code ec;
			fs::remove_all(Root, ec);
		}

		fs::path Dir(const fs::path& rel) const
		{
			fs::create_directories(Root / rel);
			return Root / rel;
		}

		fs::path Engine(const fs::path& rel) const
		{
			Dir(rel / "Engine" / "Shaders");
			return Root / rel;
		}
	};
}

TEST_CASE("Engine root: the configured path wins verbatim, even if it does not exist", "[enginepaths]")
{
	TempTree t;
	const fs::path staged = t.Engine("stage");
	CHECK(ResolveEngineRoot("D:/nowhere/at/all", staged / "bin", staged, t.Root) == fs::path("D:/nowhere/at/all"));
}

TEST_CASE("Engine root: walks up from the executable to the nearest Engine/Shaders", "[enginepaths]")
{
	TempTree t;
	const fs::path staged = t.Engine("stage");
	const fs::path exe = t.Dir("stage/build/Debug");
	CHECK(ResolveEngineRoot("", exe, {}, t.Root) == staged);
	CHECK(ResolveEngineRoot("", staged, {}, t.Root) == staged);
}

TEST_CASE("Engine root: the nearest ancestor wins over a farther one", "[enginepaths]")
{
	TempTree t;
	t.Engine("outer");
	const fs::path inner = t.Engine("outer/inner");
	CHECK(ResolveEngineRoot("", inner / "bin", {}, t.Root) == inner);
}

TEST_CASE("Engine root: a staged layout beats the baked source tree", "[enginepaths]")
{
	TempTree t;
	const fs::path staged = t.Engine("stage");
	const fs::path source = t.Engine("checkout");
	CHECK(ResolveEngineRoot("", staged / "bin", source, t.Root) == staged);
}

TEST_CASE("Engine root: no marker above the executable falls through to the source tree", "[enginepaths]")
{
	TempTree t;
	const fs::path source = t.Engine("checkout");
	const fs::path exe = t.Dir("game/build/Debug");
	CHECK(ResolveEngineRoot("", exe, source, t.Root) == source);
}

TEST_CASE("Engine root: a source tree without Engine/Shaders is ignored", "[enginepaths]")
{
	TempTree t;
	const fs::path source = t.Dir("gone");
	const fs::path exe = t.Dir("game/bin");
	const fs::path cwd = t.Dir("cwd");
	CHECK(ResolveEngineRoot("", exe, source, cwd) == cwd);
	CHECK(ResolveEngineRoot("", exe, {}, cwd) == cwd);
}

TEST_CASE("Engine root: Engine without a Shaders directory is not a marker", "[enginepaths]")
{
	TempTree t;
	t.Dir("half/Engine");
	const fs::path cwd = t.Dir("cwd");
	CHECK(ResolveEngineRoot("", t.Root / "half" / "bin", {}, cwd) == cwd);
}

TEST_CASE("Project settings live under Saved/Settings.cfg of that project", "[enginepaths]")
{
	const fs::path p = ProjectSettingsPath("Projects/Sandbox");
	CHECK(p == fs::path("Projects/Sandbox") / "Saved" / "Settings.cfg");
	CHECK(p.filename() == "Settings.cfg");
	CHECK(p.parent_path().filename() == "Saved");
	CHECK(ProjectSettingsPath("A") != ProjectSettingsPath("B"));
}

TEST_CASE("Asset paths: absolute passes through and relative resolves under the root", "[enginepaths]")
{
	const fs::path abs = fs::temp_directory_path() / "x.hlsl";
	CHECK(EngineAssetPath(abs.string()) == abs);
	CHECK(EngineAssetPath("Engine/Shaders/Foo.hlsl") == GetEngineRoot() / "Engine" / "Shaders" / "Foo.hlsl");
}
