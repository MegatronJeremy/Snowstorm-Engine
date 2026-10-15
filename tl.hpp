#pragma once



#include <algorithm>
#include <cstdint>
#include <vector>

namespace Snowstorm
{
	struct GpuZoneEvent
	{
		uint32_t Scope = 0; // index into the scope list the events were built from
		bool Begin = false;
		uint64_t Ns = 0;
	};

	// Flattens resolved scopes of ONE queue into a begin/end stream that is well nested, which is what a
	// profiler's zone stack requires. Timestamps alone cannot order coincident events, so ties break by
	// nesting: ends before begins (back-to-back passes), deepest end first, shallowest begin first. A
	// zero-length scope keeps its begin before its end. Scopes without a recorded interval are dropped.
	[[nodiscard]] inline std::vector<GpuZoneEvent> BuildGpuZoneEvents(const std::vector<GpuScope>& scopes,
	                                                                  const GpuQueue queue)
	{
		struct Keyed
		{
			GpuZoneEvent Event;
			uint32_t Phase;
			uint32_t Order;
		};
		std::vector<Keyed> keyed;
		keyed.reserve(scopes.size() * 2);
		for (uint32_t i = 0; i < scopes.size(); ++i)
		{
			const GpuScope& s = scopes[i];
			if (s.Queue != queue || s.EndNs < s.StartNs || s.EndNs == 0)
				continue;
			const bool empty = s.EndNs == s.StartNs;
			// phase 0: ends of non-empty scopes (deepest first), 1: begins and empty scopes (shallowest first)
			keyed.push_back({{i, true, s.StartNs}, 1, s.Depth * 2});
			keyed.push_back(empty ? Keyed{{i, false, s.EndNs}, 1, s.Depth * 2 + 1}
			                      : Keyed{{i, false, s.EndNs}, 0, UINT32_MAX - s.Depth});
		}
		std::stable_sort(keyed.begin(), keyed.end(), [](const Keyed& a, const Keyed& b)
		                 {
			if (a.Event.Ns != b.Event.Ns)
				return a.Event.Ns < b.Event.Ns;
			if (a.Phase != b.Phase)
				return a.Phase < b.Phase;
			return a.Order < b.Order; });
		std::vector<GpuZoneEvent> out;
		out.reserve(keyed.size());
		for (const Keyed& k : keyed)
			out.push_back(k.Event);
		return out;
	}
}
