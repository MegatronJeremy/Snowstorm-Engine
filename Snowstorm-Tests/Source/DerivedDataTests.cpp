#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/DerivedData.hpp"
#include "Snowstorm/Assets/MeshCache.hpp"
#include "Snowstorm/Assets/MeshMetaCache.hpp"
#include "Snowstorm/Assets/SourceHashIndex.hpp"
#include "Snowstorm/Utility/Hash.hpp"

#include <fstream>
#include <random>
#include <string>

using namespace Snowstorm;

namespace
{
	struct TempDir
	{
		std::filesystem::path Path;

		TempDir()
		{
			Path = std::filesystem::temp_directory_path() / ("ss_ddc_test_" + std::to_string(std::random_device{}()));
			std::filesystem::create_directories(Path);
		}
		~TempDir()
		{
			std::error_code ec;
			std::filesystem::remove_all(Path, ec);
		}
	};

	void WriteText(const std::filesystem::path& p, const std::string& text)
	{
		std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
	}
}

TEST_CASE("DerivedDataKey separates every input", "[ddc]")
{
	const int a = 1;
	const int b = 2;
	const auto key = DerivedDataKey(7, 1, std::as_bytes(std::span(&a, 1)));
	CHECK(key == DerivedDataKey(7, 1, std::as_bytes(std::span(&a, 1))));
	CHECK(key != DerivedDataKey(8, 1, std::as_bytes(std::span(&a, 1))));
	CHECK(key != DerivedDataKey(7, 2, std::as_bytes(std::span(&a, 1))));
	CHECK(key != DerivedDataKey(7, 1, std::as_bytes(std::span(&b, 1))));
}

TEST_CASE("Mesh blobs and bounds are named per submesh", "[ddc]")
{
	constexpr uint64_t source = 0xABCD;
	CHECK(MeshCacheIO::GetCachePath(source, -1) != MeshCacheIO::GetCachePath(source, 0));
	CHECK(MeshCacheIO::GetCachePath(source, 0) != MeshCacheIO::GetCachePath(source, 1));
	CHECK(MeshCacheIO::GetCachePath(source, 0) != MeshCacheIO::GetCachePath(source + 1, 0));
	CHECK(MeshMetaCacheIO::GetCachePath(source, 0).extension() == ".json");
}

TEST_CASE("WriteFileAtomic replaces the target and leaves no temp behind", "[ddc]")
{
	const TempDir dir;
	const auto target = dir.Path / "sub" / "entry.bin";
	CHECK(WriteFileAtomic(target, [](std::ostream& out)
	                      { return static_cast<bool>(out << "one"); }));
	CHECK(WriteFileAtomic(target, [](std::ostream& out)
	                      { return static_cast<bool>(out << "two"); }));
	CHECK_FALSE(WriteFileAtomic(target, [](std::ostream&)
	                            { return false; }));

	std::ifstream in(target);
	std::string text;
	in >> text;
	CHECK(text == "two");

	size_t files = 0;
	for ([[maybe_unused]] const auto& e : std::filesystem::directory_iterator(target.parent_path()))
	{
		++files;
	}
	CHECK(files == 1);
}

TEST_CASE("SourceHashIndex hashes content and trusts an unchanged stat", "[ddc]")
{
	const TempDir dir;
	const auto source = dir.Path / "source.txt";
	const auto indexFile = dir.Path / "index.json";
	WriteText(source, "hello");

	SourceHashIndex index(indexFile);
	const uint64_t first = index.Hash(source);
	CHECK(first != 0);
	CHECK(first == HashFileContents(source));
	CHECK(index.Hash(dir.Path / "missing.txt") == 0);

	// Same size and mtime: the index does not reread the file (git's racy-index tradeoff)
	const auto stamp = std::filesystem::last_write_time(source);
	WriteText(source, "jello");
	std::filesystem::last_write_time(source, stamp);
	CHECK(index.Hash(source) == first);

	// The entry persists, so a new process trusts the stat too
	index.Save();
	SourceHashIndex reloaded(indexFile);
	CHECK(reloaded.Hash(source) == first);

	// A size change rehashes
	WriteText(source, "hello, world");
	CHECK(index.Hash(source) == HashFileContents(source));
	CHECK(index.Hash(source) != first);
}
