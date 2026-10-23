#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/TextureCache.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>

using namespace Snowstorm;
namespace fs = std::filesystem;

namespace
{
	// The blob path is fixed under the engine root, so each fixture takes a fresh random handle and removes
	// its blob (and temp file) on exit.
	struct Fixture
	{
		fs::path Dir;
		fs::path Source;
		AssetHandle Handle;
		TextureCookKey Key{static_cast<uint8_t>(TextureRole::Albedo), 1, 2, kTextureEncoderRevision};

		Fixture()
		{
			static std::atomic<int> counter{0};
			Dir = fs::temp_directory_path() / ("ss_texcache_" + std::to_string(counter++) + "_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
			fs::remove_all(Dir);
			fs::create_directories(Dir);
			Source = Dir / "source.png";
			WriteSource("pixels-v1");
		}

		~Fixture()
		{
			std::error_code ec;
			fs::remove_all(Dir, ec);
			fs::remove(BlobPath(), ec);
			fs::remove(BlobPath().string() + ".tmp", ec);
			fs::remove_all(BlobPath().string() + ".tmp", ec);
		}

		fs::path BlobPath() const { return TextureCacheIO::GetCachePath(Handle, static_cast<TextureRole>(Key.Role)); }

		void WriteSource(const std::string& text) const { std::ofstream(Source, std::ios::binary) << text; }

		std::vector<char> ReadBlob() const
		{
			std::ifstream in(BlobPath(), std::ios::binary);
			return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
		}

		void WriteBlob(const std::vector<char>& bytes) const
		{
			std::ofstream out(BlobPath(), std::ios::binary | std::ios::trunc);
			out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		}
	};

	std::vector<uint8_t> Pattern(const size_t n, const uint8_t seed)
	{
		std::vector<uint8_t> v(n);
		for (size_t i = 0; i < n; ++i)
			v[i] = static_cast<uint8_t>(seed + i * 7);
		return v;
	}

	// 8x8 RGBA8 chain: 8x8, 4x4, 2x2, 1x1.
	CookedTexture MakeRgba8()
	{
		CookedTexture t;
		t.Width = 8;
		t.Height = 8;
		t.Format = CookedTexture::Encoding::RGBA8;
		for (uint32_t i = 0; i < 4; ++i)
			t.Levels.push_back(Pattern((8u >> i) * (8u >> i) * 4, static_cast<uint8_t>(i + 1)));
		return t;
	}

	// 8x8 BC1 chain: 4x4 blocks of 8 bytes, minimum one block per level.
	CookedTexture MakeBc1()
	{
		CookedTexture t;
		t.Width = 8;
		t.Height = 8;
		t.Format = CookedTexture::Encoding::BC1;
		t.Levels = {Pattern(4 * 8, 11), Pattern(8, 22), Pattern(8, 33), Pattern(8, 44)};
		return t;
	}

	void CheckSame(const CookedTexture& got, const CookedTexture& want)
	{
		CHECK(got.Width == want.Width);
		CHECK(got.Height == want.Height);
		CHECK(got.Format == want.Format);
		REQUIRE(got.Levels.size() == want.Levels.size());
		for (size_t i = 0; i < want.Levels.size(); ++i)
			CHECK(got.Levels[i] == want.Levels[i]);
	}
}

TEST_CASE("TextureCache round trips a multi-level RGBA8 chain", "[assets][texturecache]")
{
	Fixture f;
	const CookedTexture src = MakeRgba8();
	REQUIRE(TextureCacheIO::Save(f.Handle, f.Source, src, f.Key));

	const auto loaded = TextureCacheIO::Load(f.Handle, f.Source, f.Key);
	REQUIRE(loaded.has_value());
	CheckSame(*loaded, src);
	CHECK(loaded->MipLevels() == 4);
}

TEST_CASE("TextureCache round trips a BC1 chain and keeps the encoding", "[assets][texturecache]")
{
	Fixture f;
	const CookedTexture src = MakeBc1();
	REQUIRE(TextureCacheIO::Save(f.Handle, f.Source, src, f.Key));

	const auto loaded = TextureCacheIO::Load(f.Handle, f.Source, f.Key);
	REQUIRE(loaded.has_value());
	CheckSame(*loaded, src);
	CHECK(loaded->Format == CookedTexture::Encoding::BC1);
}

TEST_CASE("TextureCache: no blob is a miss", "[assets][texturecache]")
{
	Fixture f;
	CHECK_FALSE(TextureCacheIO::Load(f.Handle, f.Source, f.Key).has_value());
}

TEST_CASE("TextureCache: each cook key field mismatch is a miss", "[assets][texturecache]")
{
	Fixture f;
	REQUIRE(TextureCacheIO::Save(f.Handle, f.Source, MakeRgba8(), f.Key));
	REQUIRE(TextureCacheIO::Load(f.Handle, f.Source, f.Key).has_value());

	// Role picks the blob path, so the stored key is compared on the other three fields; a different role
	// lands on a different file and misses for that reason instead.
	{
		TextureCookKey k = f.Key;
		k.MipSpace ^= 1;
		CHECK_FALSE(TextureCacheIO::Load(f.Handle, f.Source, k).has_value());
	}
	{
		TextureCookKey k = f.Key;
		k.Compress ^= 1;
		CHECK_FALSE(TextureCacheIO::Load(f.Handle, f.Source, k).has_value());
	}
	{
		TextureCookKey k = f.Key;
		k.EncoderRevision = static_cast<uint8_t>(k.EncoderRevision + 1);
		CHECK_FALSE(TextureCacheIO::Load(f.Handle, f.Source, k).has_value());
	}
	{
		TextureCookKey k = f.Key;
		k.Role = static_cast<uint8_t>(TextureRole::Sprite);
		CHECK_FALSE(TextureCacheIO::Load(f.Handle, f.Source, k).has_value());
		CHECK(TextureCacheIO::GetCachePath(f.Handle, TextureRole::Sprite) != f.BlobPath());
	}
}

TEST_CASE("TextureCache: changed source content is a miss, a mere touch is not", "[assets][texturecache]")
{
	Fixture f;
	REQUIRE(TextureCacheIO::Save(f.Handle, f.Source, MakeRgba8(), f.Key));

	// Same bytes, new write time: the content hash keeps the blob valid.
	fs::last_write_time(f.Source, fs::file_time_type::clock::now() + std::chrono::hours(2));
	CHECK(TextureCacheIO::Load(f.Handle, f.Source, f.Key).has_value());

	// Different bytes and a different write time: re-decode.
	f.WriteSource("pixels-v2-different-length");
	fs::last_write_time(f.Source, fs::file_time_type::clock::now() + std::chrono::hours(4));
	CHECK_FALSE(TextureCacheIO::Load(f.Handle, f.Source, f.Key).has_value());
}

TEST_CASE("TextureCache: truncated blob is a miss", "[assets][texturecache]")
{
	Fixture f;
	REQUIRE(TextureCacheIO::Save(f.Handle, f.Source, MakeRgba8(), f.Key));
	const std::vector<char> full = f.ReadBlob();
	REQUIRE(full.size() > 64);

	// Cut inside the last level, inside a middle level, and inside the header.
	for (const size_t cut : {full.size() - 1, full.size() - 260, size_t{10}})
	{
		f.WriteBlob(std::vector<char>(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(cut)));
		CHECK_FALSE(TextureCacheIO::Load(f.Handle, f.Source, f.Key).has_value());
	}
}

TEST_CASE("TextureCache: damaged header is a miss", "[assets][texturecache]")
{
	Fixture f;
	REQUIRE(TextureCacheIO::Save(f.Handle, f.Source, MakeRgba8(), f.Key));
	const std::vector<char> good = f.ReadBlob();

	SECTION("magic")
	{
		std::vector<char> b = good;
		b[0] ^= 0x55;
		f.WriteBlob(b);
		CHECK_FALSE(TextureCacheIO::Load(f.Handle, f.Source, f.Key).has_value());
	}
	SECTION("version")
	{
		std::vector<char> b = good;
		b[4] ^= 0x55;
		f.WriteBlob(b);
		CHECK_FALSE(TextureCacheIO::Load(f.Handle, f.Source, f.Key).has_value());
	}
	SECTION("zero length level prefix")
	{
		// The first level prefix follows the 40-byte header (4+4+8+8, then five u32).
		std::vector<char> b = good;
		for (size_t i = 40; i < 48; ++i)
			b[i] = 0;
		f.WriteBlob(b);
		CHECK_FALSE(TextureCacheIO::Load(f.Handle, f.Source, f.Key).has_value());
	}
	SECTION("empty file")
	{
		f.WriteBlob({});
		CHECK_FALSE(TextureCacheIO::Load(f.Handle, f.Source, f.Key).has_value());
	}
}

TEST_CASE("TextureCache: Save refuses an empty texture", "[assets][texturecache]")
{
	Fixture f;
	CookedTexture noLevels = MakeRgba8();
	noLevels.Levels.clear();
	CHECK_FALSE(TextureCacheIO::Save(f.Handle, f.Source, noLevels, f.Key));

	CookedTexture noSize = MakeRgba8();
	noSize.Width = 0;
	CHECK_FALSE(TextureCacheIO::Save(f.Handle, f.Source, noSize, f.Key));
	CHECK_FALSE(fs::exists(f.BlobPath()));
}

TEST_CASE("TextureCache: Save returns false when the target is unwritable", "[assets][texturecache]")
{
	Fixture f;
	// A directory squatting on the temp name makes the open fail on every platform.
	fs::create_directories(f.BlobPath().string() + ".tmp");
	CHECK_FALSE(TextureCacheIO::Save(f.Handle, f.Source, MakeRgba8(), f.Key));
	CHECK_FALSE(fs::exists(f.BlobPath()));
}
