#pragma once

#include <string>
#include <unordered_map>

namespace Snowstorm
{
	class World;
	class RendererService;

	// The Settings window's Performance tab: frame and GPU timings, draw and cull counts, and the per-phase,
	// per-system and per-pass times as smoothed bars. Draws into the current ImGui window.
	class PerformancePanel
	{
	public:
		void OnImGuiRender(World& world, const RendererService& renderer);

	private:
		// Exponential moving average of each phase/system CPU time, keyed by a stable id ("phase:Render",
		// "sys:RenderSystem"). The raw per-frame numbers jitter; smoothing damps them into a readable value
		// and feeds the proportional bars (cf. Unreal "stat unit" / Tracy running averages).
		std::unordered_map<std::string, float> m_SmoothedMs;

		// Per-row visibility with hysteresis: an idle system (e.g. ScriptSystem with no scripts) is hidden,
		// but using TWO thresholds keyed off the slow EMA -- show above the high mark, hide below the low one
		// -- so a row hovering near the boundary can't flicker in and out (a single threshold would strobe).
		std::unordered_map<std::string, bool> m_RowVisible;
	};
}
