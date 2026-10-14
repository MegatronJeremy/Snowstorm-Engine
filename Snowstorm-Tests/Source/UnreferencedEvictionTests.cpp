#include <catch2/catch_test_macros.hpp>

#include <memory>
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
