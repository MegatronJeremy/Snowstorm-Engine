#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/Buffer.hpp"

using namespace Snowstorm;

TEST_CASE("IndexTypeSize matches the element width", "[render]")
{
	REQUIRE(IndexTypeSize(IndexType::UInt16) == 2);
	REQUIRE(IndexTypeSize(IndexType::UInt32) == 4);
}

TEST_CASE("SelectIndexType switches to 32-bit past 65536 vertices", "[render]")
{
	REQUIRE(SelectIndexType(3) == IndexType::UInt16);
	REQUIRE(SelectIndexType(65536) == IndexType::UInt16);
	REQUIRE(SelectIndexType(65537) == IndexType::UInt32);
}

TEST_CASE("PackIndices emits little-endian elements of the requested width", "[render]")
{
	const std::vector<uint32_t> idx{0, 1, 0x1234, 0xFFFE};
	const auto p16 = PackIndices(idx, IndexType::UInt16);
	REQUIRE(p16.size() == 8);
	REQUIRE(p16[4] == 0x34);
	REQUIRE(p16[5] == 0x12);
	REQUIRE(p16[6] == 0xFE);
	REQUIRE(p16[7] == 0xFF);

	const auto p32 = PackIndices(idx, IndexType::UInt32);
	REQUIRE(p32.size() == 16);
	REQUIRE(p32[8] == 0x34);
	REQUIRE(p32[9] == 0x12);
	REQUIRE(p32[10] == 0);
}
