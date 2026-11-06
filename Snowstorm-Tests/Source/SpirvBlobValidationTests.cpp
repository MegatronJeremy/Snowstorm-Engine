#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/SpirvBlobValidation.hpp"

#include <vector>

using namespace Snowstorm;

namespace
{
	constexpr uint64_t kKey = 0x1234abcd5678ef01ull;

	std::vector<uint32_t> GoodBlob()
	{
		return {kSpirvMagic, 0x00010500u, 0u, 42u, 0u, 0x0002000Eu, 1u};
	}

	size_t Bytes(const std::vector<uint32_t>& b)
	{
		return b.size() * sizeof(uint32_t);
	}

	std::optional<std::string> StampFor(const std::vector<uint32_t>& b, const uint64_t key = kKey)
	{
		return SerializeBlobStamp(MakeBlobStamp(key, b.data(), Bytes(b)));
	}
}

TEST_CASE("SpirvBlob: matching blob and stamp pass", "[shader]")
{
	const auto b = GoodBlob();
	REQUIRE(ValidateCachedSpirv(b.data(), Bytes(b), kKey, StampFor(b)) == BlobCheck::Ok);
}

TEST_CASE("SpirvBlob: stale blob (key from other source or options) falls back", "[shader]")
{
	const auto b = GoodBlob();
	REQUIRE(ValidateCachedSpirv(b.data(), Bytes(b), kKey, StampFor(b, kKey + 1)) == BlobCheck::StampKeyMismatch);
}

TEST_CASE("SpirvBlob: corrupt blobs fall back", "[shader]")
{
	const auto b = GoodBlob();
	const auto stamp = StampFor(b);

	auto flipped = b;
	flipped[6] ^= 0x10u;
	REQUIRE(ValidateCachedSpirv(flipped.data(), Bytes(flipped), kKey, stamp) == BlobCheck::StampHashMismatch);

	auto truncated = b;
	truncated.pop_back();
	REQUIRE(ValidateCachedSpirv(truncated.data(), Bytes(truncated), kKey, stamp) == BlobCheck::StampSizeMismatch);

	auto badMagic = b;
	badMagic[0] = 0xdeadbeefu;
	REQUIRE(ValidateCachedSpirv(badMagic.data(), Bytes(badMagic), kKey, StampFor(badMagic)) == BlobCheck::BadMagic);

	auto badVersion = b;
	badVersion[1] = 0x00020000u;
	REQUIRE(ValidateCachedSpirv(badVersion.data(), Bytes(badVersion), kKey, StampFor(badVersion)) ==
	        BlobCheck::BadVersion);

	auto zeroBound = b;
	zeroBound[3] = 0;
	REQUIRE(ValidateCachedSpirv(zeroBound.data(), Bytes(zeroBound), kKey, StampFor(zeroBound)) ==
	        BlobCheck::ZeroIdBound);

	const uint8_t tiny[8]{};
	REQUIRE(ValidateCachedSpirv(tiny, sizeof(tiny), kKey, stamp) == BlobCheck::TooSmall);
	REQUIRE(ValidateCachedSpirv(b.data(), Bytes(b) - 2, kKey, stamp) == BlobCheck::Misaligned);
}

TEST_CASE("SpirvBlob: missing or garbled stamp falls back", "[shader]")
{
	const auto b = GoodBlob();
	REQUIRE(ValidateCachedSpirv(b.data(), Bytes(b), kKey, std::nullopt) == BlobCheck::NoStamp);
	REQUIRE(ValidateCachedSpirv(b.data(), Bytes(b), kKey, std::string("garbage")) == BlobCheck::NoStamp);
	REQUIRE(ValidateCachedSpirv(b.data(), Bytes(b), kKey, std::string("key=zz\nsize=1\nhash=1\n")) ==
	        BlobCheck::NoStamp);
}

TEST_CASE("SpirvBlob: stamp round trips", "[shader]")
{
	const BlobStamp s{kKey, 28, 0xfeedfacecafebeefull};
	const auto parsed = ParseBlobStamp(SerializeBlobStamp(s));
	REQUIRE(parsed);
	REQUIRE(parsed->key == s.key);
	REQUIRE(parsed->size == s.size);
	REQUIRE(parsed->contentHash == s.contentHash);
}
