#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/MeshCache.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace Snowstorm;

namespace
{
	// Header layout the blob starts with: u32 magic, u32 version, u64 write time, u64 hash, u64 vertex count, u64 index count.
	constexpr size_t kVersionOffset = 4;
	constexpr size_t kVertexCountOffset = 24;
	constexpr size_t kHeaderSize = 40;

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

	CookedMesh MakeMesh()
	{
		CookedMesh m;
		for (int i = 0; i < 5; ++i)
		{
			Vertex v;
			const float f = static_cast<float>(i);
			v.Position = {f, f + 0.5f, -f};
			v.Normal = {0.0f, 1.0f, 0.0f};
			v.TexCoord = {f * 0.25f, 1.0f - f * 0.25f};
			v.Tangent = {1.0f, 0.0f, 0.0f, i % 2 ? -1.0f : 1.0f};
			m.Vertices.push_back(v);
		}
		m.Indices = {0, 1, 2, 2, 3, 4, 4, 0, 2};
		return m;
	}

	void WriteSource(const std::filesystem::path& path, const std::string& text)
	{
		std::ofstream out(path, std::ios::trunc | std::ios::binary);
		out << text;
	}

	std::string ReadAll(const std::filesystem::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
	}

	void WriteAll(const std::filesystem::path& path, const std::string& bytes)
	{
		std::ofstream out(path, std::ios::trunc | std::ios::binary);
		out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	}

	// Saves MakeMesh() for a fresh source file and returns the blob path.
	std::filesystem::path SaveFixture(const TempDir& dir, const AssetHandle handle, std::filesystem::path& source)
	{
		source = dir.Path / "cube.obj";
		WriteSource(source, "v 0 0 0\n");
		REQUIRE(MeshCacheIO::SaveTo(dir.Path, handle, source, MakeMesh()));
		return MeshCacheIO::GetCachePathIn(dir.Path, handle);
	}
}

TEST_CASE("MeshCache SaveTo then LoadFrom returns identical vertex and index data", "[meshcache]")
{
	TempDir dir("ss_meshcache_roundtrip");
	const AssetHandle h(0x1234abcdULL);
	std::filesystem::path source;
	SaveFixture(dir, h, source);

	const auto out = MeshCacheIO::LoadFrom(dir.Path, h, source);
	const CookedMesh in = MakeMesh();

	REQUIRE(out.has_value());
	REQUIRE(out->Vertices.size() == in.Vertices.size());
	CHECK(std::memcmp(out->Vertices.data(), in.Vertices.data(), in.Vertices.size() * sizeof(Vertex)) == 0);
	CHECK(out->Indices == in.Indices);
}

TEST_CASE("MeshCache SaveTo creates missing directories and leaves no temp file", "[meshcache]")
{
	TempDir dir("ss_meshcache_mkdir");
	const auto nested = dir.Path / "a" / "b";
	const AssetHandle h(77);
	const auto source = dir.Path / "m.obj";
	WriteSource(source, "x");

	REQUIRE(MeshCacheIO::SaveTo(nested, h, source, MakeMesh()));
	CHECK(std::filesystem::exists(MeshCacheIO::GetCachePathIn(nested, h)));
	CHECK_FALSE(std::filesystem::exists(MeshCacheIO::GetCachePathIn(nested, h).string() + ".tmp"));
}

TEST_CASE("MeshCache SaveTo refuses an empty mesh and writes nothing", "[meshcache]")
{
	TempDir dir("ss_meshcache_empty");
	const AssetHandle h(3);
	const auto source = dir.Path / "m.obj";
	WriteSource(source, "x");

	CookedMesh noIndices = MakeMesh();
	noIndices.Indices.clear();
	CookedMesh noVertices = MakeMesh();
	noVertices.Vertices.clear();

	CHECK_FALSE(MeshCacheIO::SaveTo(dir.Path, h, source, noIndices));
	CHECK_FALSE(MeshCacheIO::SaveTo(dir.Path, h, source, noVertices));
	CHECK_FALSE(std::filesystem::exists(MeshCacheIO::GetCachePathIn(dir.Path, h)));
}

TEST_CASE("MeshCache LoadFrom is a miss for a handle never saved", "[meshcache]")
{
	TempDir dir("ss_meshcache_missing");
	CHECK_FALSE(MeshCacheIO::LoadFrom(dir.Path, AssetHandle(9), dir.Path / "none.obj").has_value());
}

TEST_CASE("MeshCache LoadFrom rejects a truncated blob", "[meshcache]")
{
	TempDir dir("ss_meshcache_truncated");
	const AssetHandle h(11);
	std::filesystem::path source;
	const auto blob = SaveFixture(dir, h, source);
	const std::string bytes = ReadAll(blob);
	REQUIRE(bytes.size() > kHeaderSize);

	SECTION("in the index array")
	{
		WriteAll(blob, bytes.substr(0, bytes.size() - 1));
	}
	SECTION("in the vertex array")
	{
		WriteAll(blob, bytes.substr(0, kHeaderSize + sizeof(Vertex)));
	}
	SECTION("inside the header")
	{
		WriteAll(blob, bytes.substr(0, kHeaderSize - 1));
	}
	SECTION("empty file")
	{
		WriteAll(blob, "");
	}

	CHECK_FALSE(MeshCacheIO::LoadFrom(dir.Path, h, source).has_value());
}

TEST_CASE("MeshCache LoadFrom rejects a wrong magic or version", "[meshcache]")
{
	TempDir dir("ss_meshcache_header");
	const AssetHandle h(12);
	std::filesystem::path source;
	const auto blob = SaveFixture(dir, h, source);
	std::string bytes = ReadAll(blob);

	SECTION("magic")
	{
		bytes[0] ^= 0x5A;
	}
	SECTION("version")
	{
		bytes[kVersionOffset] ^= 0x01;
	}

	WriteAll(blob, bytes);
	CHECK_FALSE(MeshCacheIO::LoadFrom(dir.Path, h, source).has_value());
}

TEST_CASE("MeshCache LoadFrom rejects a size field that disagrees with the payload", "[meshcache]")
{
	TempDir dir("ss_meshcache_sizefield");
	const AssetHandle h(13);
	std::filesystem::path source;
	const auto blob = SaveFixture(dir, h, source);
	std::string bytes = ReadAll(blob);

	SECTION("vertex count larger than the payload")
	{
		const uint64_t count = MakeMesh().Vertices.size() + 1000;
		std::memcpy(&bytes[kVertexCountOffset], &count, sizeof(count));
	}
	SECTION("vertex count zero")
	{
		const uint64_t count = 0;
		std::memcpy(&bytes[kVertexCountOffset], &count, sizeof(count));
	}
	SECTION("index count zero")
	{
		const uint64_t count = 0;
		std::memcpy(&bytes[kVertexCountOffset + sizeof(uint64_t)], &count, sizeof(count));
	}

	WriteAll(blob, bytes);
	CHECK_FALSE(MeshCacheIO::LoadFrom(dir.Path, h, source).has_value());
}

TEST_CASE("MeshCache LoadFrom is a miss once the source content changes", "[meshcache]")
{
	TempDir dir("ss_meshcache_stale");
	const AssetHandle h(14);
	std::filesystem::path source;
	SaveFixture(dir, h, source);
	REQUIRE(MeshCacheIO::LoadFrom(dir.Path, h, source).has_value());

	// Different length and content, and the mtime pushed forward so the cheap gate cannot pass it.
	WriteSource(source, "v 1 2 3\nv 4 5 6\n");
	std::filesystem::last_write_time(source, std::filesystem::last_write_time(source) + std::chrono::hours(1));

	CHECK_FALSE(MeshCacheIO::LoadFrom(dir.Path, h, source).has_value());
}

TEST_CASE("MeshCache Save overwrites an earlier blob for the same handle", "[meshcache]")
{
	TempDir dir("ss_meshcache_overwrite");
	const AssetHandle h(15);
	std::filesystem::path source;
	SaveFixture(dir, h, source);

	CookedMesh second = MakeMesh();
	second.Indices = {0, 1, 2};
	REQUIRE(MeshCacheIO::SaveTo(dir.Path, h, source, second));

	const auto out = MeshCacheIO::LoadFrom(dir.Path, h, source);
	REQUIRE(out.has_value());
	CHECK(out->Indices == second.Indices);
}
