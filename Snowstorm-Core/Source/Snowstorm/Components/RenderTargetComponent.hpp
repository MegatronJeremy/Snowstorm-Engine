#pragma once

#include "Snowstorm/Render/RenderTarget.hpp"

namespace Snowstorm
{
	struct RenderTargetComponent
	{
		Ref<RenderTarget> Target;
		entt::entity TargetEntity{}; // NEW: Points to the entity holding the ViewportComponent

		RenderTargetComponent() = default;
		explicit RenderTargetComponent(Ref<RenderTarget> target) : Target(std::move(target)) {}
	};
}
