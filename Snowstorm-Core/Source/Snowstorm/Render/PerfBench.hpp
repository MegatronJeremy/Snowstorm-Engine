#pragma once

#include "Snowstorm/Render/CommandContext.hpp" // GpuScope

#include <glm/vec3.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Snowstorm
{
	// Everything about a run that is NOT a timing: what it ran on and what it looked at. The script gates
	// on all of it, since a ms number only means something against a baseline captured at the same
	// resolution and viewpoint (the engine renders at the host's viewport size, which no CVar pins).
	struct PerfBenchRunInfo
	{
		std::string Device;               // GPU name; slugged into the baseline directory
		std::string Config;               // rung the runner set ("rt-off", "+gi", "shadows-stoch")
		bool TimestampsSupported = false; // false => passes are empty, script skips instead of failing
		uint32_t Width = 0;
		uint32_t Height = 0;
		glm::vec3 CameraPosition{};
		glm::vec3 CameraRotation{}; // Euler radians, matching the camera.override wire format
	};

	// Headless GPU perf-benchmark accumulator + serializer (pure, engine-free logic so it's unit-testable,
	// following the WriteNpy / HaltonJitter pattern). Application::Run feeds it the per-frame GpuScope list
	// (RendererService::GetGpuPassTimes) over a sample window; on exit it serializes averaged per-pass ms
	// to a deterministic JSON that Scripts/perf-bench.py diffs against a committed baseline. No file I/O or
	// GPU calls here beyond the final ToJson string build; the caller owns the ofstream.
	class PerfBenchAccumulator
	{
	public:
		// Fold one frame's resolved scopes into the running per-name stats. Call once per sampled frame
		// (after warmup). A scope absent this frame simply doesn't advance its count, so averages are per
		// the frames that actually recorded it, which is correct for conditionally-present passes.
		void AddFrame(const std::vector<GpuScope>& scopes, float gpuFrameMs)
		{
			for (const GpuScope& s : scopes)
			{
				PassStat& ps = m_Passes[s.Name];
				ps.SumMs += s.Milliseconds;
				ps.MinMs = ps.Count == 0 ? s.Milliseconds : (s.Milliseconds < ps.MinMs ? s.Milliseconds : ps.MinMs);
				ps.MaxMs = ps.Count == 0 ? s.Milliseconds : (s.Milliseconds > ps.MaxMs ? s.Milliseconds : ps.MaxMs);
				ps.Depth = s.Depth;
				ps.SumFrags += static_cast<double>(s.FragInvocations); // per-pass FS invocations (overdraw metric)
				++ps.Count;
			}
			m_TotalGpuSumMs += gpuFrameMs;
			++m_FrameCount;
		}

		[[nodiscard]] uint32_t FrameCount() const { return m_FrameCount; }
		[[nodiscard]] bool Empty() const { return m_Passes.empty(); }

		// Serialize to a small deterministic JSON that Scripts/perf-bench.py diffs. Keys are sorted
		// (std::map) so the output is stable and diffable.
		[[nodiscard]] std::string ToJson(const PerfBenchRunInfo& info) const;

	private:
		struct PassStat
		{
			double SumMs = 0.0;
			float MinMs = 0.0f;
			float MaxMs = 0.0f;
			double SumFrags = 0.0; // summed fragment-shader invocations (avg written to JSON)
			uint32_t Count = 0;
			uint32_t Depth = 0;
		};

		std::map<std::string, PassStat> m_Passes; // sorted by name -> deterministic JSON order
		double m_TotalGpuSumMs = 0.0;
		uint32_t m_FrameCount = 0;
	};
}
