#pragma once

#include <cstdint>
#include <unordered_map>

#include <entt/entt.hpp>

namespace Snowstorm
{
	using UuidEntityMap = std::unordered_map<uint64_t, entt::entity>;

	// Viewports below 64x64 get no render targets.
	inline bool IsValidViewportSize(const uint32_t w, const uint32_t h)
	{
		return w >= 64u && h >= 64u;
	}

	// UUID 0 is "unset" and is never mapped. A duplicate UUID keeps the entity registered last.
	inline void RegisterUuid(UuidEntityMap& map, const uint64_t uuid, const entt::entity e)
	{
		if (uuid != 0)
			map[uuid] = e;
	}

	// entt::null for an unset (0) or unknown UUID.
	inline entt::entity ResolveUuid(const UuidEntityMap& map, const uint64_t uuid)
	{
		if (uuid == 0)
			return entt::null;
		const auto it = map.find(uuid);
		return it != map.end() ? it->second : static_cast<entt::entity>(entt::null);
	}
}
