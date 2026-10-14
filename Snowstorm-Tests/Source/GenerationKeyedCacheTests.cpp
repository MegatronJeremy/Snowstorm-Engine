#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/GenerationKeyedCache.hpp"

#include <vector>

using namespace Snowstorm;

TEST_CASE("GenerationKeyedCache keeps entries while the generation is unchanged", "[render]")
{
	GenerationKeyedCache<int, std::vector<int>> cache;
	cache.Acquire(1, 0).push_back(7);

	int evictions = 0;
	auto& again = cache.Acquire(1, 0, [&](std::vector<int>&)
	                            { ++evictions; });
	REQUIRE(evictions == 0);
	REQUIRE(again == std::vector<int>{7});
}

TEST_CASE("GenerationKeyedCache evicts and resets on a generation change", "[render]")
{
	GenerationKeyedCache<int, std::vector<int>> cache;
	cache.Acquire(1, 0).push_back(7);
	cache.Acquire(2, 0).push_back(9);

	std::vector<int> seen;
	auto& fresh = cache.Acquire(1, 1, [&](std::vector<int>& old)
	                            { seen = old; });
	REQUIRE(seen == std::vector<int>{7});
	REQUIRE(fresh.empty());

	// The other key is untouched, and the new generation is stable.
	REQUIRE(cache.Acquire(2, 0) == std::vector<int>{9});
	fresh.push_back(3);
	REQUIRE(cache.Acquire(1, 1) == std::vector<int>{3});
}
