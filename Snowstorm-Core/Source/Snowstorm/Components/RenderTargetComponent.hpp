#pragma once

#include "Snowstorm/Render/RenderTarget.hpp"

namespace Snowstorm
{
	// Runtime-only component
	struct RenderTargetComponent
	{
		Ref<RenderTarget> Target;
	};
}
