#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/MeshMetaCache.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

using namespace Snowstorm;

namespace
{
	struct TempDir
	{
		std::filesystem::path Path;

		explicit TempDir(const char* name)
		    : Path(std::filesystem::temp_directory_path() / name)
		{
			std::filesystem::remove_all(Path);
			std::filesystem::create_directories(Path);
		}

		~TempDir()
		{
			std::error_code ec;
			std::filesystem::remove_all(Path, ec);
		}
	};

	MeshMetaCache MakeMeta(const AssetHandle handle)
	{
		MeshMetaCache m;
		m.Handle = handle;
		m.SourcePath = "Projects/Sandbox/assets/meshes/Crate.obj";
		m.SourceWriteTime = 133456789012345678ull;
		m.Bounds.Box.Min = {-1.5f, 0.25f, -2.0f};
		m.Bounds.Box.Max = {1.5f, 3.0f, 2.0f};
		m.Bounds.Sphere.Center = {0.0f, 1.625f, 0.0f};
		m.Bounds.Sphere.Radius = 3.5f;
		return m;
	}

	void WriteFile(const std::filesystem::path& path, const std::string& text)
	{
		std::ofstream out(path, std::ios::trunc | std::ios::binary);
		out << text;
	}
}

TEST_CASE("MeshMetaCache Save then Load returns identical fields", "[meshmetacache]")
{
	TempDir dir("ss_meshmeta_roundtrip");
	const AssetHandle h(0x1234abcdULL);
	const MeshMetaCache in = MakeMeta(h);

	REQUIRE(MeshMetaCacheIO::SaveTo(dir.Path, in));
	const auto out = MeshMetaCacheIO::LoadFrom(dir.Path, h);

	REQUIRE(out.has_value());
	CHECK(out->Handle == h);
	CHECK(out->SourcePath == in.SourcePath);
	CHECK(out->SourceWriteTime == in.SourceWriteTime);
	CHECK(out->Bounds.Box.Min == in.Bounds.Box.Min);
	CHECK(out->Bounds.Box.Max == in.Bounds.Box.Max);
	CHECK(out->Bounds.Sphere.Center == in.Bounds.Sphere.Center);
	CHECK(out->Bounds.Sphere.Radius == in.Bounds.Sphere.Radius);
}

TEST_CASE("MeshMetaCache SaveTo creates missing directories and leaves no temp file", "[meshmetacache]")
{
	TempDir dir("ss_meshmeta_mkdir");
	const auto nested = dir.Path / "a" / "b";
	const AssetHandle h(77);

	REQUIRE(MeshMetaCacheIO::SaveTo(nested, MakeMeta(h)));
	CHECK(std::filesystem::exists(MeshMetaCacheIO::GetCachePathIn(nested, h)));
	CHECK_FALSE(std::filesystem::exists(MeshMetaCacheIO::GetCachePathIn(nested, h).string() + ".tmp"));
}

TEST_CASE("MeshMetaCache Save overwrites an earlier entry for the same handle", "[meshmetacache]")
{
	TempDir dir("ss_meshmeta_overwrite");
	const AssetHandle h(5);
	MeshMetaCache first = MakeMeta(h);
	MeshMetaCache second = MakeMeta(h);
	second.SourceWriteTime = first.SourceWriteTime + 1;

	REQUIRE(MeshMetaCacheIO::SaveTo(dir.Path, first));
	REQUIRE(MeshMetaCacheIO::SaveTo(dir.Path, second));

	const auto out = MeshMetaCacheIO::LoadFrom(dir.Path, h);
	REQUIRE(out.has_value());
	CHECK(out->SourceWriteTime == second.SourceWriteTime);
}

TEST_CASE("MeshMetaCache Load of a missing file is nullopt", "[meshmetacache]")
{
	TempDir dir("ss_meshmeta_missing");
	CHECK_FALSE(MeshMetaCacheIO::LoadFrom(dir.Path, AssetHandle(99)).has_value());
}

TEST_CASE("MeshMetaCache Load of a damaged file is nullopt and does not throw", "[meshmetacache]")
{
	TempDir dir("ss_meshmeta_corrupt");
	const AssetHandle h(42);
	const auto path = MeshMetaCacheIO::GetCachePathIn(dir.Path, h);

	REQUIRE(MeshMetaCacheIO::SaveTo(dir.Path, MakeMeta(h)));
	std::string good;
	{
		std::ifstream in(path, std::ios::binary);
		good.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}
	REQUIRE(good.size() > 20);

	SECTION("truncated mid-document")
	{
		WriteFile(path, good.substr(0, good.size() / 2));
	}
	SECTION("empty")
	{
		WriteFile(path, "");
	}
	SECTION("not json")
	{
		WriteFile(path, "this is not json");
	}
	SECTION("json but not an object")
	{
		WriteFile(path, "[1,2,3]");
	}
	SECTION("wrong version")
	{
		WriteFile(path, R"({"Version":999,"SourceWriteTime":1,"Bounds":{}})");
	}
	SECTION("missing SourceWriteTime")
	{
		WriteFile(path, R"({"Version":1,"Bounds":{}})");
	}
	SECTION("missing Bounds")
	{
		WriteFile(path, R"({"Version":1,"SourceWriteTime":1})");
	}
	SECTION("a bounds vector with the wrong arity")
	{
		WriteFile(path, R"({"Version":1,"SourceWriteTime":1,"Bounds":{"Min":[0,0],"Max":[1,1,1],"SphereCenter":[0,0,0],"SphereRadius":1}})");
	}
	SECTION("a field of the wrong type")
	{
		WriteFile(path, R"({"Version":1,"SourceWriteTime":"soon","Bounds":{}})");
	}

	std::optional<MeshMetaCache> out;
	CHECK_NOTHROW(out = MeshMetaCacheIO::LoadFrom(dir.Path, h));
	CHECK_FALSE(out.has_value());
}

TEST_CASE("MeshMetaCache cache path is <dir>/<handle>.json", "[meshmetacache]")
{
	const AssetHandle h(0xabcULL);
	const auto p = MeshMetaCacheIO::GetCachePathIn("cache", h);
	CHECK(p.parent_path() == std::filesystem::path("cache"));
	CHECK(p.filename().string() == h.ToString() + ".json");
}
