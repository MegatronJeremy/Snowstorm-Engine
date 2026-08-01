#pragma once

#include "Snowstorm/Render/Texture.hpp"

#include <cstdint>

namespace Snowstorm
{
	// One signal's SVGF denoiser state (#132), bundled so GI, reflections, and (future) AO each own an
	// identical set instead of RenderTargetComponent carrying flat per-signal fields. All three ping-pongs are
	// parity-indexed by frameCounter&1; the pass writes the CURRENT slot and reprojects the PREVIOUS one.
	//   History[2] — accumulated signal (.rgb) + variance (.a, produced by the temporal pass for the à-trous).
	//   Moments[2] — SVGF luminance moments: .r=μ1, .g=μ2, .b=history length, .a=prev NDC depth (for reproject).
	//   Scratch[2] — à-trous ping-pong; the final filtered result lands in Scratch[0] (parity-seeded).
	// HistoryValid is the "has this instance accumulated at least once" flag — previously a
	// std::unordered_set<entt::entity> side-table on each effect (#125/#129); now it lives HERE, next to the
	// buffers it guards, keyed structurally by the viewport entity that owns this component. Reset on a scene
	// cut / when temporal toggles off (so re-enabling can't reproject stale history). Width/Height are this
	// instance's allocation extent, so the resize guard can rebuild on its own scale change.
	//
	// Runtime-only data (all Ref<> GPU handles): RenderTargetComponent that holds these is unregistered +
	// never serialized, so nesting this is reflection/serialization-safe.
	struct DenoiserInstance
	{
		Ref<Texture> History[2];
		Ref<TextureView> HistoryView[2];
		Ref<Texture> Moments[2];
		Ref<TextureView> MomentsView[2];
		Ref<Texture> Scratch[2];
		Ref<TextureView> ScratchView[2];

		bool HistoryValid = false; // false until accumulated once; reset on scene cut / temporal-off

		uint32_t Width = 0;
		uint32_t Height = 0;

		// True once every buffer + view is allocated (the resize guard's null check for this instance).
		[[nodiscard]] bool Allocated() const
		{
			return History[0] && History[1] && Moments[0] && Moments[1] && Scratch[0] && Scratch[1];
		}
	};
}
