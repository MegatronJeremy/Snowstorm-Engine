#pragma once

#include "Snowstorm/Components/SpriteComponent.hpp"

namespace Snowstorm
{
	enum class SpriteResolveAction : uint8_t
	{
		None,
		Drop,
		Resolve
	};

	// Texture 0 never touches a view game code assigned: only a view the system itself set (ResolvedFrom != 0) is dropped.
	inline SpriteResolveAction DecideSpriteResolve(const SpriteComponent& s)
	{
		if (s.Texture == 0)
			return s.ResolvedFrom != 0 ? SpriteResolveAction::Drop : SpriteResolveAction::None;
		if (s.ResolvedFrom == s.Texture && s.View)
			return SpriteResolveAction::None;
		return SpriteResolveAction::Resolve;
	}
}
