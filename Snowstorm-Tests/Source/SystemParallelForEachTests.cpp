#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/ECS/System.hpp"
#include "Snowstorm/World/World.hpp"

#include <vector>

using namespace Snowstorm;

namespace
{
	struct Counter
	{
		int Value = 0;
	};

	struct Step
	{
		int Value = 0;
	};

	class ParallelForEachTestSystem final : public System
	{
	public:
		using System::System;

		void Execute(Timestep) override {}

		void Increment()
		{
			ParallelForEach<Write<Counter>, Read<Step>>(
			    [](const entt::entity, Counter& counter, const Step& step)
			    {
				    counter.Value += step.Value;
			    });
		}

		[[nodiscard]] size_t ScratchCapacity() const { return ParallelEntityScratchCapacity(); }
	};

	class ScopedSerialEcs final
	{
	public:
		ScopedSerialEcs()
		    : m_Previous(CVars::EcsParallel.Get())
		{
			// Avoid requiring an Application/JobSystem in this headless unit test. The snapshot storage and
			// post-loop change marking are identical on the serial and parallel execution paths.
			CVars::EcsParallel.Set(false);
		}

		~ScopedSerialEcs()
		{
			CVars::EcsParallel.Set(m_Previous);
		}

	private:
		bool m_Previous;
	};
}

TEST_CASE("ParallelForEach reuses its entity snapshot after warmup", "[ecs][parallel]")
{
	ScopedSerialEcs serial;
	World world;
	auto& registry = world.GetRegistry();

	constexpr size_t entityCount = 1024;
	std::vector<entt::entity> entities;
	entities.reserve(entityCount);
	for (size_t i = 0; i < entityCount; ++i)
	{
		const entt::entity entity = registry.create();
		registry.emplace<Counter>(entity);
		registry.emplace<Step>(entity, static_cast<int>(i % 7 + 1));
		entities.push_back(entity);
	}
	registry.ClearTrackedComponents();

	ParallelForEachTestSystem system(&world);
	system.Increment(); // warmup: grows the scratch vector once
	const size_t warmCapacity = system.ScratchCapacity();

	REQUIRE(warmCapacity >= entityCount);
	for (const entt::entity entity : entities)
	{
		REQUIRE(registry.Read<Counter>(entity).Value == registry.Read<Step>(entity).Value);
		REQUIRE(registry.WasChanged<Counter>(entity));
		REQUIRE_FALSE(registry.WasChanged<Step>(entity));
	}

	registry.ClearTrackedComponents();
	system.Increment(); // same-size steady-state pass must reuse the retained capacity

	REQUIRE(system.ScratchCapacity() == warmCapacity);
	for (const entt::entity entity : entities)
	{
		REQUIRE(registry.Read<Counter>(entity).Value == registry.Read<Step>(entity).Value * 2);
		REQUIRE(registry.WasChanged<Counter>(entity));
	}
}
