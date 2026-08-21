#pragma once

#include "Snowstorm/ECS/System.hpp"
#include "Snowstorm/Render/AccelerationStructure.hpp"

namespace Snowstorm
{
	// Builds and maintains the scene's top-level acceleration structure (#118). Runs in PreRender, before
	// the RT shadow pass consumes the TLAS. Mirrors VisibilitySystem's dirty-signal pattern: it rebuilds the
	// TLAS only when the renderable set or any transform changed (Init/Fini/ChangedView on Mesh/Transform),
	// gathers one instance per (Transform + resolved-Mesh) entity, builds each mesh's BLAS lazily, and points
	// the bindless TLAS slot at the result. No-op unless some RT effect is active (RT shadows/AO/reflections/
	// GI): nothing samples the TLAS otherwise, so building it there is pure waste (BLAS builds + a TLAS build
	// per scene change). The first frame after any RT effect turns ON forces a rebuild.
	class TlasBuildSystem final : public System
	{
	public:
		explicit TlasBuildSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

		// TLAS builds must reflect authored transforms in the editor too (the RT shadows should match the
		// raster ones while editing), so this runs in edit mode.
		[[nodiscard]] bool RunsInEditMode() const override { return true; }

	private:
		// True when a rebuild is warranted: any add/remove/change to the transforms or meshes that make up
		// the instance set. Cheap early-out on a static scene (same pattern as VisibilitySystem).
		[[nodiscard]] bool IsSceneDirtyThisFrame() const;

		Ref<TLAS> m_TLAS; // created lazily on first RT-enabled build; scene-scoped
		bool m_BuiltOnce = false;
		bool m_WasRTActive = false;              // RT-active state last frame — detects the off->on edge to force a rebuild
		bool m_LastOmmEnabled = true;            // render.omm last build — a toggle forces a rebuild (OMM vs any-hit)
		uint32_t m_LastLoggedCount = UINT32_MAX; // de-dupe the instance-count log across per-frame rebuilds
	};
}
