#pragma once

#include <cstdint>
#include <vector>

namespace Snowstorm
{
	struct QueueFamilyCaps
	{
		bool Graphics = false;
		bool Compute = false;
		bool Transfer = false;
	};

	struct QueueFamilySelection
	{
		uint32_t Graphics;
		uint32_t Transfer;
		uint32_t Compute;
		bool DedicatedTransfer() const { return Transfer != Graphics; }
		bool DedicatedCompute() const { return Compute != Graphics; }
	};

	// First family with the capability and without the ones that would time-slice it against render work
	// (a TRANSFER-only family for DMA, a COMPUTE-without-GRAPHICS family for async compute). A family that
	// does not qualify leaves the graphics family aliased in, which callers read as "run inline".
	inline QueueFamilySelection SelectQueueFamilies(const std::vector<QueueFamilyCaps>& families, uint32_t graphicsFamily)
	{
		QueueFamilySelection s{graphicsFamily, graphicsFamily, graphicsFamily};
		for (uint32_t i = 0; i < families.size(); ++i)
		{
			const QueueFamilyCaps& f = families[i];
			if (f.Transfer && !f.Graphics && !f.Compute && s.Transfer == graphicsFamily)
				s.Transfer = i;
			if (f.Compute && !f.Graphics && s.Compute == graphicsFamily)
				s.Compute = i;
		}
		return s;
	}
}
