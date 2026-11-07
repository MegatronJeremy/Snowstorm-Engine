#pragma once

namespace Snowstorm
{
	class RendererService;

	// The Settings window's render settings: one widget per render CVar, grouped by technique. Draws into the
	// current ImGui window. Stateless, since the CVars are the state.
	void DrawRenderSettings(const RendererService& renderer);
}
