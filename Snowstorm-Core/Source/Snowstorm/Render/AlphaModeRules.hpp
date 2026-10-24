#pragma once

#include "Snowstorm/Assets/MaterialAsset.hpp"

namespace Snowstorm
{
	// Only Blend needs its own pipeline (blend on, depth write off); Opaque and Mask share one.
	[[nodiscard]] constexpr bool AlphaModeUsesBlendPipeline(const AlphaMode mode)
	{
		return mode == AlphaMode::Blend;
	}

	// The GPU layouts carry only the mask flag, so Blend is opaque to every shader consumer.
	[[nodiscard]] constexpr bool AlphaModeSetsMaskFlag(const AlphaMode mode)
	{
		return mode == AlphaMode::Mask;
	}

	// A Blend material routes through the sorted transparent pass only with render.transparent on and a built
	// blend pipeline; otherwise it renders as opaque.
	[[nodiscard]] constexpr bool IsSortedTransparentRule(const bool transparentPassOn, const AlphaMode mode, const bool hasBlendPipeline)
	{
		return transparentPassOn && AlphaModeUsesBlendPipeline(mode) && hasBlendPipeline;
	}
}
