#pragma once

namespace Snowstorm
{
	// Base for the first-class render passes extracted out of RendererSingleton. Each pass owns its own
	// pipelines/targets/state and is held by RenderSystem, so it persists across frames and tears down in
	// the normal device-shutdown window (Renderer::WaitIdle drains the GPU before worlds/systems die).
	//
	// The interface is deliberately minimal: passes have heterogeneous inputs (target formats, the World,
	// light/environment blocks), so each exposes its own typed Execute(...) rather than a forced uniform
	// signature that would devolve into a context bag. The shared base exists for ownership + identification
	// (logging/diagnostics) and so the pass set can grow uniformly as more features are extracted.
	class IRenderPass
	{
	public:
		virtual ~IRenderPass() = default;

		[[nodiscard]] virtual const char* Name() const = 0;
	};
}
