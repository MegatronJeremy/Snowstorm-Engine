#pragma once

#include "Snowstorm/Render/RendererUtils.hpp"

#include <cstdint>

namespace Snowstorm
{
	// Pure decisions behind ViewportResizeSystem: when a viewport is visited, which size it takes, and
	// which targets need a rebuild. The system supplies the CVar values and the current target extents.
	namespace ViewportResizeRule
	{
		struct Extent
		{
			uint32_t W = 0;
			uint32_t H = 0;
			constexpr bool operator==(const Extent& o) const { return W == o.W && H == o.H; }
			constexpr bool operator!=(const Extent& o) const { return !(*this == o); }
		};

		// An ImGui panel below 64 px (collapsed, minimised) never gets a target.
		constexpr bool ValidViewportSize(uint32_t w, uint32_t h)
		{
			return w >= 64 && h >= 64;
		}

		// False when the whole pass can be skipped this frame.
		constexpr bool ShouldRun(bool anyViewportChanged, bool cameraInitialising, bool globalScaleChanged)
		{
			return anyViewportChanged || cameraInitialising || globalScaleChanged;
		}

		// With ImGui a viewport is revisited only on its own change or a global trigger; without it the
		// viewport follows the window, so every one is visited.
		constexpr bool ShouldVisit(bool imguiEnabled, bool viewportChanged, bool cameraInitialising, bool globalScaleChanged)
		{
			return !imguiEnabled || viewportChanged || cameraInitialising || globalScaleChanged;
		}

		// Size the targets are built at. ImGui: the panel size, or nothing when it is too small. No ImGui:
		// the window size, replaced by the forced resolution when both its dimensions are positive.
		struct Resolved
		{
			bool Valid = false;
			Extent Size;
		};

		constexpr Resolved ResolveSize(bool imguiEnabled, Extent panel, Extent window, int forcedW, int forcedH)
		{
			if (imguiEnabled)
			{
				return {ValidViewportSize(panel.W, panel.H), panel};
			}
			if (forcedW > 0 && forcedH > 0)
			{
				return {true, {static_cast<uint32_t>(forcedW), static_cast<uint32_t>(forcedH)}};
			}
			return {true, window};
		}

		struct Scales
		{
			float Scene = 1.0f;
			float GI = 1.0f;
			float AO = 1.0f;
			float Shadow = 1.0f;
		};

		struct Extents
		{
			Extent Scene;
			Extent GI;
			Extent AO;
			Extent Shadow;
		};

		inline Extents ScaledExtents(Extent full, const Scales& s)
		{
			return {
			    {ScaledExtent(full.W, s.Scene), ScaledExtent(full.H, s.Scene)},
			    {ScaledExtent(full.W, s.GI), ScaledExtent(full.H, s.GI)},
			    {ScaledExtent(full.W, s.AO), ScaledExtent(full.H, s.AO)},
			    {ScaledExtent(full.W, s.Shadow), ScaledExtent(full.H, s.Shadow)},
			};
		}

		// What exists now. A false Has* means that target is not allocated, so it never counts as changed
		// (the Missing flag covers it).
		struct Current
		{
			bool Missing = false;
			bool HasPresent = false;
			Extent Present;
			bool HasScene = false;
			Extent Scene;
			bool HasGI = false;
			Extent GI;
			bool HasAO = false;
			Extent AO;
			bool HasShadow = false;
			Extent Shadow;
			bool HasMsaaSource = false;
			uint32_t Msaa = 1;
		};

		inline bool NeedsRebuild(const Current& c, Extent full, const Extents& want, uint32_t wantMsaa)
		{
			const bool viewportResized = c.HasPresent && c.Present != full;
			const bool sceneChanged = c.HasScene && c.Scene != want.Scene;
			const bool giChanged = c.HasGI && c.GI != want.GI;
			const bool aoChanged = c.HasAO && c.AO != want.AO;
			const bool shadowChanged = c.HasShadow && c.Shadow != want.Shadow;
			const bool msaaChanged = c.HasMsaaSource && c.Msaa != wantMsaa;
			return c.Missing || viewportResized || sceneChanged || giChanged || aoChanged || shadowChanged || msaaChanged;
		}
	}
}
