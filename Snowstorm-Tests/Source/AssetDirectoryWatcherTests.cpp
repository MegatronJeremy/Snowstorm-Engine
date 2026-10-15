#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/AssetDirectoryWatcher.hpp"

#include <fstream>

using namespace Snowstorm;

TEST_CASE("ChangeDebouncer reports a new file only after it settles", "[assets][watcher]")
{
	ChangeDebouncer d(0.5);
	d.Seed({{"a.png", {1, 10}}});

	DirectorySnapshot s{{"a.png", {1, 10}}, {"b.obj", {5, 100}}};
	CHECK(d.Update(s, 0.0).Empty()); // first sighting
	CHECK(d.Update(s, 0.3).Empty()); // not yet stable long enough

	const auto c = d.Update(s, 0.6);
	REQUIRE(c.Settled.size() == 1);
	CHECK(c.Settled[0] == "b.obj");
	CHECK(d.Update(s, 1.2).Empty()); // reported once
}

TEST_CASE("ChangeDebouncer restarts the clock while a file is still being written", "[assets][watcher]")
{
	ChangeDebouncer d(0.5);
	d.Seed({});
	CHECK(d.Update({{"m.fbx", {1, 10}}}, 0.0).Empty());
	CHECK(d.Update({{"m.fbx", {2, 50}}}, 0.4).Empty()); // grew
	CHECK(d.Update({{"m.fbx", {2, 50}}}, 0.8).Empty()); // only 0.4 s since last change
	CHECK(d.Update({{"m.fbx", {2, 50}}}, 1.0).Settled.size() == 1);
}

TEST_CASE("ChangeDebouncer reports modification and removal", "[assets][watcher]")
{
	ChangeDebouncer d(0.0);
	d.Seed({{"a.png", {1, 10}}, {"b.png", {1, 10}}});

	DirectorySnapshot s{{"a.png", {2, 10}}};
	const auto first = d.Update(s, 0.0); // removal is reported immediately, modification waits to settle
	REQUIRE(first.Removed.size() == 1);
	CHECK(first.Removed[0] == "b.png");
	CHECK(first.Settled.empty());

	const auto c = d.Update(s, 0.1);
	REQUIRE(c.Settled.size() == 1);
	CHECK(c.Settled[0] == "a.png");
	CHECK(c.Removed.empty());
}

TEST_CASE("ScanDirectory skips the named directory and rejected files", "[assets][watcher]")
{
	namespace fs = std::filesystem;
	const fs::path root = fs::temp_directory_path() / "ss_watcher_scan_test";
	fs::remove_all(root);
	fs::create_directories(root / "cache");
	fs::create_directories(root / "sub");
	std::ofstream(root / "a.png") << "x";
	std::ofstream(root / "sub" / "b.png") << "yy";
	std::ofstream(root / "notes.txt") << "z";
	std::ofstream(root / "cache" / "c.png") << "w";

	const auto snap = ScanDirectory(root, "cache", [](const fs::path& p)
	                                { return p.extension() == ".png"; });
	CHECK(snap.size() == 2);
	fs::remove_all(root);
}
