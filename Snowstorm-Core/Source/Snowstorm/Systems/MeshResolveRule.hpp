#pragma once

#include <cstdint>

namespace Snowstorm
{
	enum class MeshResolveAction : uint8_t
	{
		None,   // nothing to write
		Clear,  // handle is 0 and an instance is still bound: drop it
		Retry,  // handle set but the mesh is not resident yet: leave the instance, ask again next frame
		Assign, // resolved mesh differs from the bound instance: write it
	};

	// resolvedAvailable: GetMeshAsync returned non-null. resolvedMatchesBound: it is the instance already bound.
	constexpr MeshResolveAction DecideMeshResolve(const uint64_t handle, const bool hasInstance,
	                                              const bool resolvedAvailable, const bool resolvedMatchesBound)
	{
		if (handle == 0)
		{
			return hasInstance ? MeshResolveAction::Clear : MeshResolveAction::None;
		}
		if (!resolvedAvailable)
		{
			return MeshResolveAction::Retry;
		}
		return resolvedMatchesBound ? MeshResolveAction::None : MeshResolveAction::Assign;
	}

	// The per-frame safety net: handle and instance disagree on whether a mesh is bound.
	constexpr bool MeshNeedsSafetyNetResolve(const uint64_t handle, const bool hasInstance)
	{
		return (handle != 0) != hasInstance;
	}
}
