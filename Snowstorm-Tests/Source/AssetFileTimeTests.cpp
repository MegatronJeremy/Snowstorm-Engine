#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/AssetFileTime.hpp"
#include "Snowstorm/Utility/Hash.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <string>

using namespace Snowstorm;
namespace fs = std::filesystem;

namespace
{
	struct TempDir
	{
		fs::path Root;

		TempDir()
		{
			static std::atomic<int> counter{0};
			Root = fs::temp_directory_path() / ("ss_filetime_" + std::to_string(counter++) + "_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
			fs::remove_all(Root);
			fs::create_directories(Root);
		}

		~TempDir()
		{
			std::error_code ec;
			fs::remove_all(Root, ec);
		}
	};

	void WriteText(const fs::path& path, const std::string& text)
	{
		std::ofstream(path, std::ios::binary) << text;
	}

	// Sets the stamp explicitly so no test depends on the filesystem's timestamp granularity.
	void SetWriteTime(const fs::path& path, std::chrono::hours offset)
	{
		fs::last_write_time(path, fs::file_time_type::clock::now() + offset);
	}
}

TEST_CASE("GetFileWriteTimeU64: missing file reads 0", "[assets][filetime]")
{
	TempDir dir;
	CHECK(GetFileWriteTimeU64(dir.Root / "absent.bin") == 0);
}

TEST_CASE("GetFileWriteTimeU64: stable across reads, changes after the stamp moves", "[assets][filetime]")
{
	TempDir dir;
	const auto file = dir.Root / "a.bin";
	WriteText(file, "one");
	SetWriteTime(file, std::chrono::hours(-2));

	const uint64_t first = GetFileWriteTimeU64(file);
	CHECK(first != 0);
	CHECK(GetFileWriteTimeU64(file) == first);

	SetWriteTime(file, std::chrono::hours(-1));
	const uint64_t later = GetFileWriteTimeU64(file);
	CHECK(later != first);
	CHECK(later > first);
}

TEST_CASE("SourceIsUnchanged: matching mtime is valid without reading the source", "[assets][filetime]")
{
	TempDir dir;
	const auto file = dir.Root / "a.bin";
	WriteText(file, "payload");
	const uint64_t stamp = GetFileWriteTimeU64(file);

	// The recorded hash is deliberately wrong: a matching mtime must short-circuit before hashing.
	CHECK(SourceIsUnchanged(file, stamp, 0));
	CHECK(SourceIsUnchanged(file, stamp, 0xDEADBEEFull));
}

TEST_CASE("SourceIsUnchanged: moved mtime falls back to the content hash", "[assets][filetime]")
{
	TempDir dir;
	const auto file = dir.Root / "a.bin";
	WriteText(file, "payload");
	SetWriteTime(file, std::chrono::hours(-3));
	const uint64_t oldStamp = GetFileWriteTimeU64(file);
	const uint64_t hash = HashFileContents(file);
	REQUIRE(hash != 0);

	// A touch or a fresh checkout: new timestamp, same bytes.
	SetWriteTime(file, std::chrono::hours(-1));
	REQUIRE(GetFileWriteTimeU64(file) != oldStamp);
	CHECK(SourceIsUnchanged(file, oldStamp, hash));

	SECTION("changed content is stale")
	{
		WriteText(file, "payload!");
		SetWriteTime(file, std::chrono::hours(-1));
		CHECK_FALSE(SourceIsUnchanged(file, oldStamp, hash));
	}

	SECTION("a recorded hash of 0 trusts the mtime only")
	{
		CHECK_FALSE(SourceIsUnchanged(file, oldStamp, 0));
	}

	SECTION("a recorded mtime of 0 skips the gate and uses the hash")
	{
		CHECK(SourceIsUnchanged(file, 0, hash));
		CHECK_FALSE(SourceIsUnchanged(file, 0, 0));
	}
}

TEST_CASE("SourceIsUnchanged: missing source is never unchanged", "[assets][filetime]")
{
	TempDir dir;
	const auto file = dir.Root / "gone.bin";
	CHECK_FALSE(SourceIsUnchanged(file, 0, 0));
	CHECK_FALSE(SourceIsUnchanged(file, 12345, 0));
	CHECK_FALSE(SourceIsUnchanged(file, 12345, 0xABCDull));
}
