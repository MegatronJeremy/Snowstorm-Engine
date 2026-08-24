#pragma once

#include "Snowstorm/Render/CommandContext.hpp" // GpuScope

#include <glm/vec3.hpp>

#include <cstdint>
#include <deque>
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
	// Rolling-window steady-state detector for the perf-benchmark warmup.
	//
	// A fixed warmup assumes a settling time instead of observing one. 15 frames is ~0.25s, which does not
	// reach GPU DVFS steady state, so the run averages part of the clock ramp and the answer depends on how
	// warm the machine happened to be. The error is also invisible: the JSON looks identical either way.
	// This waits until frame time actually stops moving, the way quality-bench decides an image has
	// converged rather than guessing a frame count.
	//
	// Steady == the rolling window's peak-to-peak spread falls below `epsilon` of its mean. Peak-to-peak
	// rather than a variance measure because one slow frame is exactly what should keep us out of steady
	// state, and an outlier-tolerant statistic would smooth that away. `maxFrames` bounds the wait so a
	// machine that never settles still produces a run, flagged as not-settled instead of silently passing
	// as clean.
	class SteadyStateDetector
	{
	public:
		SteadyStateDetector(const uint32_t window, const float epsilon, const uint32_t minFrames, const uint32_t maxFrames)
		    : m_Window(window == 0 ? 1 : window), m_Epsilon(epsilon), m_MinFrames(minFrames), m_MaxFrames(maxFrames)
		{
		}

		// Feed one frame's GPU time. Returns true once warmup is over (settled OR the cap was hit).
		bool Update(const float frameMs)
		{
			++m_Observed;
			if (frameMs > 0.0f) // frames before the first timestamp resolve report 0 and carry no signal
			{
				m_Samples.push_back(frameMs);
				if (m_Samples.size() > m_Window)
				{
					m_Samples.pop_front();
				}
			}

			if (m_Observed >= m_MaxFrames)
			{
				m_TimedOut = true;
				return true;
			}
			if (m_Observed < m_MinFrames || m_Samples.size() < m_Window)
			{
				return false;
			}

			float lo = m_Samples.front();
			float hi = m_Samples.front();
			double sum = 0.0;
			for (const float v : m_Samples)
			{
				lo = v < lo ? v : lo;
				hi = v > hi ? v : hi;
				sum += static_cast<double>(v);
			}
			const double mean = sum / static_cast<double>(m_Samples.size());
			return mean > 0.0 && (static_cast<double>(hi) - static_cast<double>(lo)) / mean <= static_cast<double>(m_Epsilon);
		}

		[[nodiscard]] uint32_t FramesObserved() const { return m_Observed; }
		[[nodiscard]] bool TimedOut() const { return m_TimedOut; }

	private:
		std::deque<float> m_Samples;
		size_t m_Window;
		float m_Epsilon;
		uint32_t m_MinFrames;
		uint32_t m_MaxFrames;
		uint32_t m_Observed = 0;
		bool m_TimedOut = false;
	};

	// Headless GPU perf-benchmark accumulator + serializer (pure, engine-free logic so it's unit-testable
	// — the WriteNpy / HaltonJitter pattern). Application::Run feeds it the per-frame GpuScope list
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
