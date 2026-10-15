#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "Platform/Vulkan/VulkanSmallBufferPool.hpp"

using namespace Snowstorm;

TEST_CASE("Small host buffer pool routing", "[vulkan][alloc]")
{
	SECTION("small host-visible write buffers use the pool")
	{
		REQUIRE(UsesSmallHostBufferPool(512, true, false));
		REQUIRE(UsesSmallHostBufferPool(kSmallHostBufferMaxSize, true, false));
	}
	SECTION("larger, device-local, readback and empty buffers do not")
	{
		REQUIRE_FALSE(UsesSmallHostBufferPool(kSmallHostBufferMaxSize + 1, true, false));
		REQUIRE_FALSE(UsesSmallHostBufferPool(512, false, false));
		REQUIRE_FALSE(UsesSmallHostBufferPool(512, true, true));
		REQUIRE_FALSE(UsesSmallHostBufferPool(0, true, false));
	}
	SECTION("a block holds many threshold-sized buffers")
	{
		const uint64_t buffersPerBlock = kSmallHostBufferBlockSize / kSmallHostBufferMaxSize;
		REQUIRE(buffersPerBlock >= 16u);
	}
}
