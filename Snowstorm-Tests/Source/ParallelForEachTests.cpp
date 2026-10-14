#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/ECS/System.hpp"

using namespace Snowstorm;

namespace
{
	struct Counter
	{
		int value = 0;
	};

	struct Step
	{
		int by = 1;
	};

	class CounterSystem final : public System
	{
	public:
		using System::System;

		void Execute(Timestep) override
		{
			ParallelForEach<Write<Counter>, Read<Step>>([](entt::entity, Counter& c, const Step& s)
			                                            { c.value += s.by; });
		}

		void RunOther()
		{
			ParallelForEach<Write<Counter>>([](entt::entity, Counter& c)
			                                { c.value += 100; });
		}

		[[nodiscard]] size_t ScratchCapacity() const { return ParallelScratchCapacity(); }
	};
}

TEST_CASE("ParallelForEach reuses its snapshot buffer and stays correct across calls", "[ecs]")
{
	World world;
	auto& reg = world.GetRegistry();
	constexpr int count = 1000;
	for (int i = 0; i < count; ++i)
	{
		const auto e = reg.create();
		reg.emplace<Counter>(e);
		if (i % 2 == 0)
		{
			reg.emplace<Step>(e, i);
		}
	}

	CounterSystem system(&world);
	system.Execute(Timestep{});
	const size_t capacityAfterFirst = system.ScratchCapacity();
	REQUIRE(capacityAfterFirst >= count / 2);

	system.Execute(Timestep{});
	system.RunOther();
	const size_t capacityAfterWide = system.ScratchCapacity();
	REQUIRE(capacityAfterWide >= count);
	system.Execute(Timestep{});
	REQUIRE(system.ScratchCapacity() == capacityAfterWide);

	int i = 0;
	for (const auto e : reg.view<Counter>())
	{
		(void)e;
		++i;
	}
	REQUIRE(i == count);
	for (const auto e : reg.view<Counter, Step>())
	{
		REQUIRE(reg.get<Counter>(e).value == reg.get<Step>(e).by * 3 + 100);
	}
}
