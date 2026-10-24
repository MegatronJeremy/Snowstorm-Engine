#pragma once

#include <cstdint>

namespace Snowstorm::TlasRebuildRule
{
	// One frame's change signals, already reduced to flags by TlasBuildSystem.
	struct SceneChanges
	{
		bool MeshChanged = false;
		bool MeshOrTransformInit = false;
		bool MeshOrTransformFini = false;
		bool AnyDestroyed = false; // registry.destroy() bypasses FiniView
		bool MaterialChanged = false;
		bool MeshTransformChanged = false; // a changed Transform on an entity that also has a Mesh
	};

	// A changed Transform alone does not count: the camera's is rewritten every frame it moves and is no instance.
	[[nodiscard]] constexpr bool IsSceneDirty(const SceneChanges& c)
	{
		return c.MeshChanged || c.MeshOrTransformInit || c.MeshOrTransformFini || c.AnyDestroyed || c.MaterialChanged ||
		       c.MeshTransformChanged;
	}

	struct RebuildState
	{
		bool RtActive = false;
		bool WasRtActive = false;
		bool BuiltOnce = false;
		bool OmmEnabled = true;
		bool LastOmmEnabled = true;
		uint32_t PendingLoads = 0;
		uint32_t PrevPendingLoads = 0;
	};

	[[nodiscard]] constexpr bool JustEnabled(const RebuildState& s)
	{
		return s.RtActive && !s.WasRtActive;
	}

	// The OMM edge counts only once a build exists to be swapped.
	[[nodiscard]] constexpr bool OmmToggled(const RebuildState& s)
	{
		return s.BuiltOnce && s.OmmEnabled != s.LastOmmEnabled;
	}

	// Streaming runs one frame past the drain so the just-resident albedo gets its final rebuild (OMM bake).
	[[nodiscard]] constexpr bool Streaming(const RebuildState& s)
	{
		return s.PendingLoads > 0 || s.PrevPendingLoads > 0;
	}

	// Evaluated only while RT is active; an inactive frame never reaches this.
	[[nodiscard]] constexpr bool ShouldRebuild(const RebuildState& s, const bool sceneDirty)
	{
		return !s.BuiltOnce || JustEnabled(s) || OmmToggled(s) || Streaming(s) || sceneDirty;
	}
}
