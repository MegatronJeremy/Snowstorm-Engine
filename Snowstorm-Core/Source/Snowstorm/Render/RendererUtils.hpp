#pragma once
#include "RenderTarget.hpp"
#include "Snowstorm/Core/Base.hpp"

namespace Snowstorm
{
	Ref<RenderTarget> CreateDefaultSceneRenderTarget(uint32_t w, uint32_t h, const char* debugPrefix);
}
