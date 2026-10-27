#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <unordered_set>
#include <vector>
#include <string>
#include <unordered_map>

#include "Snowstorm/Assets/UnreferencedEviction.hpp"

using namespace Snowstorm;

namespace
{
	using Cache = std::unordered_map<std::string, std::shared_ptr<int>>;
	constexpr auto kNoop = [](const std::string&, const std::shared_ptr<int>&) {};
	constexpr auto kNoExtra = [](const std::shared_ptr<int>&) -> size_t
	{ return 0; };
}

TEST_CASE("eviction drops only entries nobody else references", "[eviction]")
{
	Cache cache;
	cache["a"] = std::make_shared<int>(1);
	cache["b"] = std::make_shared<int>(2);
	const auto heldB = cache["b"];
	RetireQueue<std::shared_ptr<int>> retired;

	REQUIRE(EvictUnreferenced(cache, retired, 2, kNoop, kNoExtra) == 1);
	REQUIRE_FALSE(cache.contains("a"));
	REQUIRE(cache.contains("b"));
	REQUIRE(retired.Size() == 1);
}

TEST_CASE("evicted resource outlives the retire delay", "[eviction]")
{
	Cache cache;
	cache["a"] = std::make_shared<int>(1);
	const std::weak_ptr<int> watch = cache["a"];
	RetireQueue<std::shared_ptr<int>> retired;

	EvictUnreferenced(cache, retired, 2, kNoop, kNoExtra);
	REQUIRE_FALSE(watch.expired());
	retired.Advance();
	REQUIRE_FALSE(watch.expired());
	retired.Advance();
	REQUIRE(watch.expired());
	REQUIRE(retired.Size() == 0);
}

TEST_CASE("eviction reports each removed key and a second pass is a no-op", "[eviction]")
{
	Cache cache;
	cache["a"] = std::make_shared<int>(1);
	cache["b"] = std::make_shared<int>(2);
	RetireQueue<std::shared_ptr<int>> retired;
	size_t seen = 0;

	REQUIRE(EvictUnreferenced(cache, retired, 1, [&](const std::string&, const std::shared_ptr<int>&)
	                          { ++seen; }, kNoExtra) == 2);
	REQUIRE(seen == 2);
	REQUIRE(EvictUnreferenced(cache, retired, 1, kNoop, kNoExtra) == 0);
}

TEST_CASE("a reference held by a second cache does not keep an entry alive", "[eviction]")
{
	Cache cache;
	Cache other;
	cache["a"] = std::make_shared<int>(1);
	other["a"] = cache["a"];
	RetireQueue<std::shared_ptr<int>> retired;

	const auto oneExtra = [](const std::shared_ptr<int>&) -> size_t
	{ return 1; };
	REQUIRE(EvictUnreferenced(cache, retired, 1, kNoop, oneExtra) == 1);
	REQUIRE(other["a"].use_count() == 2); // second cache + retire queue
}

TEST_CASE("held-by variant evicts what only the second cache references and reports it", "[eviction]")
{
	std::unordered_map<std::string, std::shared_ptr<int>> cache;
	cache["free"] = std::make_shared<int>(1);
	cache["shared"] = std::make_shared<int>(2);
	cache["used"] = std::make_shared<int>(3);
	const int* const sharedPtr = cache["shared"].get();
	const int* const freePtr = cache["free"].get();
	const std::shared_ptr<int> otherCache = cache["shared"]; // second cache's reference
	const std::shared_ptr<int> component = cache["used"];
	const std::unordered_set<const int*> cacheHeld{sharedPtr};
	RetireQueue<std::shared_ptr<int>> retired;
	std::vector<const int*> evicted;

	REQUIRE(EvictUnreferencedHeldBy(cache, retired, 2, cacheHeld, evicted) == 2);
	REQUIRE(cache.size() == 1);
	REQUIRE(cache.contains("used"));
	REQUIRE(retired.Size() == 2);
	REQUIRE(evicted.size() == 2);
	REQUIRE(((evicted[0] == sharedPtr && evicted[1] == freePtr) || (evicted[0] == freePtr && evicted[1] == sharedPtr)));
}

TEST_CASE("held-by variant keeps an object a cache-held entry also has a component on", "[eviction]")
{
	std::unordered_map<std::string, std::shared_ptr<int>> cache;
	cache["a"] = std::make_shared<int>(1);
	const std::shared_ptr<int> otherCache = cache["a"];
	const std::shared_ptr<int> component = cache["a"];
	const std::unordered_set<const int*> cacheHeld{cache["a"].get()};
	RetireQueue<std::shared_ptr<int>> retired;
	std::vector<const int*> evicted;

	REQUIRE(EvictUnreferencedHeldBy(cache, retired, 1, cacheHeld, evicted) == 0);
	REQUIRE(evicted.empty());
	REQUIRE(cache.contains("a"));
}

TEST_CASE("a null cache entry is skipped, never evicted", "[eviction]")
{
	std::unordered_map<std::string, std::shared_ptr<int>> cache;
	cache["null"] = nullptr;
	RetireQueue<std::shared_ptr<int>> retired;
	std::vector<const int*> evicted;

	REQUIRE(EvictUnreferencedHeldBy(cache, retired, 1, {}, evicted) == 0);
	REQUIRE(cache.size() == 1);
}
