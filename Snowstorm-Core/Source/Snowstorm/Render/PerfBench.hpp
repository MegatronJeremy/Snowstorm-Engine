#pragma once

#include "Snowstorm/Render/CommandContext.hpp" // GpuScope

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Snowstorm
{
	// Headless GPU perf-benchmark accumulator + serializer (pure, engine-free logic so it's unit-testable
	// — the WriteNpy / HaltonJitter pattern). Application::Run feeds it the per-frame GpuScope list
	// (RendererService::GetGpuPassTimes) over a sample window; on exit it serializes averaged per-pass ms
	// to a deterministic JSON that Scripts/perf-bench.py diffs against a committed baseline. No file I/O or
	// GPU calls here beyond the final ToJson string build; the caller owns the ofstream.
	class PerfBenchAccumulator
	{
	public:
		// Fold one frame's resolved scopes into the running per-name stats. Call once per sampled frame
		// (after warmup). A scope absent this frame simply doesn't advance its count — averages are per the
		// frames that actually recorded it, which is correct for conditionally-present passes.
		void AddFrame(const std::vector<GpuScope>& scopes, float gpuFrameMs)
		{
			for (const GpuScope& s : scopes)
			{
				PassStat& ps = m_Passes[s.Name];
				ps.SumMs += s.Milliseconds;
				ps.MinMs = ps.Count == 0 ? s.Milliseconds : (s.Milliseconds < ps.MinMs ? s.Milliseconds : ps.MinMs);
				ps.MaxMs = ps.Count == 0 ? s.Milliseconds : (s.Milliseconds > ps.MaxMs ? s.Milliseconds : ps.MaxMs);
				ps.Depth = s.Depth;
				++ps.Count;
			}
			m_TotalGpuSumMs += gpuFrameMs;
			++m_FrameCount;
		}

		[[nodiscard]] uint32_t FrameCount() const { return m_FrameCount; }
		[[nodiscard]] bool Empty() const { return m_Passes.empty(); }

		// Serialize to a small deterministic JSON. `device` is the GPU name (for per-machine baselines);
		// `config` labels the RT-effect combo the runner set (e.g. "rt-off", "+gi"). `timestampsSupported`
		// false => the device lacks GPU timestamps and passes are empty; emit the flag so the script skips
		// rather than false-failing. Keys are sorted (std::map) so the output is stable/diffable.
		[[nodiscard]] std::string ToJson(const std::string& device, const std::string& config,
		                                 bool timestampsSupported) const;

	private:
		struct PassStat
		{
			double SumMs = 0.0;
			float MinMs = 0.0f;
			float MaxMs = 0.0f;
			uint32_t Count = 0;
			uint32_t Depth = 0;
		};

		std::map<std::string, PassStat> m_Passes; // sorted by name -> deterministic JSON order
		double m_TotalGpuSumMs = 0.0;
		uint32_t m_FrameCount = 0;
	};
}
