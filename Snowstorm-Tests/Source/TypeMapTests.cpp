#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Snowstorm/Utility/TypeMap.hpp"

using namespace Snowstorm;

namespace
{
	struct Item
	{
		virtual ~Item() = default;
		virtual std::string Name() const = 0;
	};

	struct Alpha final : Item
	{
		explicit Alpha(const int v = 0)
		    : Value(v)
		{
		}
		std::string Name() const override { return "Alpha"; }
		int Value;
	};

	struct Beta final : Item
	{
		explicit Beta(int* destroyed = nullptr)
		    : Destroyed(destroyed)
		{
		}
		~Beta() override
		{
			if (Destroyed)
				++*Destroyed;
		}
		std::string Name() const override { return "Beta"; }
		int* Destroyed;
	};

	struct Gamma final : Item
	{
		std::string Name() const override { return "Gamma"; }
	};
}

TEST_CASE("TypeMap Emplace forwards constructor arguments and Get returns the instance", "[typemap]")
{
	TypeMap<Item> map;
	Alpha& a = map.Emplace<Alpha>(7);
	REQUIRE(a.Value == 7);
	REQUIRE(&map.Get<Alpha>() == &a);
	REQUIRE(map.Contains<Alpha>());
	REQUIRE_FALSE(map.Contains<Beta>());
}

TEST_CASE("TypeMap Emplace of a present type returns the existing instance and ignores arguments", "[typemap]")
{
	TypeMap<Item> map;
	Alpha& first = map.Emplace<Alpha>(1);
	Alpha& second = map.Emplace<Alpha>(99);
	REQUIRE(&first == &second);
	REQUIRE(second.Value == 1);

	int count = 0;
	map.ForEach([&](const Item&)
	            { ++count; });
	REQUIRE(count == 1);
}

TEST_CASE("TypeMap ForEach visits in insertion order", "[typemap]")
{
	TypeMap<Item> map;
	map.Emplace<Gamma>();
	map.Emplace<Alpha>();
	map.Emplace<Beta>();

	std::vector<std::string> order;
	map.ForEach([&](const Item& item)
	            { order.push_back(item.Name()); });
	REQUIRE(order == std::vector<std::string>{"Gamma", "Alpha", "Beta"});
}

TEST_CASE("TypeMap destroys its items polymorphically", "[typemap]")
{
	int destroyed = 0;
	{
		TypeMap<Item> map;
		map.Emplace<Beta>(&destroyed);
		REQUIRE(destroyed == 0);
	}
	REQUIRE(destroyed == 1);
}
