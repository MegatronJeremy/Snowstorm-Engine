#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Snowstorm/Utility/Hash.hpp"

using namespace Snowstorm;

namespace
{
	uint64_t HashString(const std::string& s)
	{
		return Hash64(s.data(), s.size());
	}

	struct TempFile
	{
		std::filesystem::path Path;

		explicit TempFile(const std::vector<uint8_t>& bytes)
		{
			Path = std::filesystem::temp_directory_path() / ("ss_hash_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + ".bin");
			std::ofstream out(Path, std::ios::binary);
			out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		}

		~TempFile()
		{
			std::error_code ec;
			std::filesystem::remove(Path, ec);
		}
	};
}

// The offset basis in Hash.cpp is 1469598103934665603, the standard FNV-1a basis (14695981039346656037) with its last
// digit dropped, so these are not the published vectors. They pin the values cache keys already depend on.
TEST_CASE("Hash64 values are pinned, since they key the cook cache", "[hash]")
{
	REQUIRE(Hash64(nullptr, 0) == 0x14650fb0739d0383ull);
	REQUIRE(HashString("a") == 0x44bd8ad473cd9906ull);
	REQUIRE(HashString("foobar") == 0x88fad7c0a8ff07f2ull);
}

TEST_CASE("Hash64 depends on length and content", "[hash]")
{
	const uint8_t zeros[2] = {0, 0};
	REQUIRE(Hash64(zeros, 1) != Hash64(zeros, 2));
	REQUIRE(HashString("ab") != HashString("ba"));
	REQUIRE(HashString("foobar") == HashString("foobar"));
}

TEST_CASE("HashFileContents equals Hash64 of the file's bytes", "[hash]")
{
	SECTION("small file")
	{
		const std::vector<uint8_t> bytes = {'f', 'o', 'o', 'b', 'a', 'r'};
		const TempFile file(bytes);
		REQUIRE(HashFileContents(file.Path) == Hash64(bytes.data(), bytes.size()));
	}

	SECTION("larger than the 64 KiB read buffer, not a multiple of it")
	{
		std::vector<uint8_t> bytes(3 * 64 * 1024 + 123);
		for (size_t i = 0; i < bytes.size(); ++i)
		{
			bytes[i] = static_cast<uint8_t>(i * 31 + (i >> 8));
		}
		const TempFile file(bytes);
		REQUIRE(HashFileContents(file.Path) == Hash64(bytes.data(), bytes.size()));
	}

	SECTION("empty file hashes as empty input")
	{
		const TempFile file({});
		REQUIRE(HashFileContents(file.Path) == 0x14650fb0739d0383ull);
	}
}

TEST_CASE("HashFileContents returns 0 for an unreadable path", "[hash]")
{
	const auto missing = std::filesystem::temp_directory_path() / "ss_hash_no_such_file.bin";
	std::error_code ec;
	std::filesystem::remove(missing, ec);
	REQUIRE(HashFileContents(missing) == 0);
}
