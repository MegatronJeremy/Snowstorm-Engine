#include "VulkanTracyGpuZones.hpp"

#include "Snowstorm/Render/GpuZoneTimeline.hpp"

#if defined(TRACY_ENABLE)
#include <tracy/TracyC.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#endif

namespace Snowstorm
{
#if defined(TRACY_ENABLE)
	namespace
	{
		constexpr uint8_t kGraphicsContext = 0;
		constexpr uint8_t kComputeContext = 1;
		bool s_ContextsCreated = false;
		uint16_t s_QueryId = 0;

		void NewContext(const uint8_t id, const char* name, const int64_t baseNs)
		{
			___tracy_gpu_new_context_data ctx{};
			ctx.gpuTime = baseNs;
			ctx.period = 1.0f; // timestamps are already converted to ns
			ctx.context = id;
			ctx.flags = 0;
			ctx.type = 0;
			___tracy_emit_gpu_new_context_serial(ctx);

			___tracy_gpu_context_name_data nameData{};
			nameData.context = id;
			nameData.name = name;
			nameData.len = static_cast<uint16_t>(std::strlen(name));
			___tracy_emit_gpu_context_name_serial(nameData);
		}

		void EmitQueue(const std::vector<GpuScope>& scopes, const GpuQueue queue, const uint8_t context)
		{
			for (const GpuZoneEvent& e : BuildGpuZoneEvents(scopes, queue))
			{
				const uint16_t query = s_QueryId++;
				if (e.Begin)
				{
					const std::string& name = scopes[e.Scope].Name;
					const uint64_t srcloc = ___tracy_alloc_srcloc_name(0, "", 0, "", 0, name.c_str(), name.size(), 0);
					___tracy_gpu_zone_begin_data begin{};
					begin.srcloc = srcloc;
					begin.queryId = query;
					begin.context = context;
					___tracy_emit_gpu_zone_begin_alloc_serial(begin);
				}
				else
				{
					___tracy_gpu_zone_end_data end{};
					end.queryId = query;
					end.context = context;
					___tracy_emit_gpu_zone_end_serial(end);
				}
				___tracy_gpu_time_data time{};
				time.gpuTime = static_cast<int64_t>(e.Ns);
				time.queryId = query;
				time.context = context;
				___tracy_emit_gpu_time_serial(time);
			}
		}
	}

	void TracyGpuZones::Emit(const std::vector<GpuScope>& scopes)
	{
		if (scopes.empty() || !___tracy_connected())
			return;

		if (!s_ContextsCreated)
		{
			// Tracy anchors a context's first gpuTime to the CPU time it is created at. Both queues share one
			// device clock, so they get the same anchor and line up with each other.
			uint64_t base = UINT64_MAX;
			for (const GpuScope& s : scopes)
				if (s.EndNs != 0)
					base = std::min(base, s.StartNs);
			if (base == UINT64_MAX)
				return;
			NewContext(kGraphicsContext, "Vulkan graphics queue", static_cast<int64_t>(base));
			NewContext(kComputeContext, "Vulkan async compute queue", static_cast<int64_t>(base));
			s_ContextsCreated = true;
		}

		EmitQueue(scopes, GpuQueue::Graphics, kGraphicsContext);
		EmitQueue(scopes, GpuQueue::AsyncCompute, kComputeContext);
	}
#else
	void TracyGpuZones::Emit(const std::vector<GpuScope>&) {}
#endif
}
