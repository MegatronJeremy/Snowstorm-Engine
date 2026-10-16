#pragma once

#include <Snowstorm/ECS/System.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace Snowstorm
{
	// Layers one phase's systems (given in registration order) into stages. Every conflicting pair keeps
	// its registration order (earlier system in an earlier stage); non-conflicting systems may share a
	// stage. Stage k holds the systems whose conflicting predecessors are all in stages < k, and indices
	// inside a stage are ascending. Undeclared and structural systems conflict with everything, so each
	// one lands alone.
	using SystemSchedule = std::vector<std::vector<size_t>>;

	[[nodiscard]] inline SystemSchedule BuildSchedule(const std::vector<SystemAccess>& systems)
	{
		SystemSchedule stages;
		std::vector<size_t> stageOf(systems.size(), 0);
		for (size_t j = 0; j < systems.size(); ++j)
		{
			size_t stage = 0;
			for (size_t i = 0; i < j; ++i)
			{
				if (systems[i].ConflictsWith(systems[j]))
				{
					stage = std::max(stage, stageOf[i] + 1);
				}
			}
			stageOf[j] = stage;
			if (stage >= stages.size())
			{
				stages.resize(stage + 1);
			}
			stages[stage].push_back(j);
		}
		return stages;
	}

	// Execution order for one phase: the systems flagged `active` (edit-mode gate applied before staging,
	// so a skipped system never holds up its dependents), stage by stage, as indices into `systems`.
	// Serial execution of this order is the schedule with threads off.
	[[nodiscard]] inline std::vector<size_t> ScheduleOrder(const std::vector<SystemAccess>& systems, const std::vector<bool>& active)
	{
		std::vector<SystemAccess> live;
		std::vector<size_t> origin;
		for (size_t i = 0; i < systems.size(); ++i)
		{
			if (active[i])
			{
				live.push_back(systems[i]);
				origin.push_back(i);
			}
		}
		std::vector<size_t> order;
		order.reserve(live.size());
		for (const auto& stage : BuildSchedule(live))
		{
			for (const size_t k : stage)
			{
				order.push_back(origin[k]);
			}
		}
		return order;
	}
}
