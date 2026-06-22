#pragma once

#include <cstdint>

namespace Snowstorm
{
	// Execution phases for systems. Systems run phase by phase in the order below;
	// within a phase they run in registration order. This makes ordering explicit
	// (no more "register me before X" comments) and lets the editor contribute systems
	// to a phase (e.g. UI) that is simply empty in a packaged runtime.
	enum class SystemPhase : uint8_t
	{
		Init,       // one-time / lifecycle resolve (e.g. RuntimeInitSystem)
		Logic,      // scripts, input-driven controllers
		AssetSync,  // hot-reload, shader/asset watchers
		UI,         // editor / ImGui systems — EMPTY in a packaged runtime
		Resolve,    // turn handles into runtime resources (mesh / material / camera)
		PreRender,  // lighting, visibility / culling, pre-draw controllers
		Render,     // submit (RenderSystem — always last)
		_Count
	};
}
