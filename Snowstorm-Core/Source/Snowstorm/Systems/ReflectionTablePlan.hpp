#pragma once

#include <cstdint>

namespace Snowstorm
{
	struct ReflectionTablePlan
	{
		bool Recreate = false;    // allocate a new buffer of NewCapacity records
		bool Publish = false;     // write the records and publish the buffer address
		uint32_t NewCapacity = 0; // capacity after the plan (unchanged unless Recreate)
	};

	// An empty gather publishes nothing (the caller zeroes the address) and keeps the cached buffer, so a later
	// non-empty gather that fits reuses it without recreating.
	constexpr ReflectionTablePlan PlanReflectionTable(const uint32_t recordCount, const bool hasTable,
	                                                  const uint32_t capacity)
	{
		ReflectionTablePlan plan;
		plan.NewCapacity = capacity;
		if (recordCount == 0)
		{
			return plan;
		}
		plan.Publish = true;
		if (!hasTable || capacity < recordCount)
		{
			plan.Recreate = true;
			plan.NewCapacity = recordCount;
		}
		return plan;
	}
}
