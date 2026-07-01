#pragma once

#include "Snowstorm/ECS/Singleton.hpp"

namespace Snowstorm
{
	// Editor-wide Edit/Simulate state. In Edit (the default) the scene is being authored: simulation
	// systems (scripts, rotators — anything that returns RunsInEditMode() == false) are skipped, so the
	// authored scene doesn't move under the cursor and the gizmo doesn't fight an animation. In Simulate
	// the whole system set runs so the world animates, but — unlike a true Play-In-Editor — the EDITOR
	// camera and selection/gizmo stay live; you observe the running world rather than possessing a player
	// (this engine has no player/pawn concept). Matches Unreal's "Simulate" (vs PIE). This is also the gate
	// the temporal-upscaling work needs: motion (jitter / history accumulation) is tied to Simulate.
	//
	// Editor-only: a packaged runtime never creates this singleton, and SystemManager treats "no singleton"
	// as "everything runs", so the gate is a no-op outside the editor.
	class EditorStateSingleton final : public Singleton
	{
	public:
		enum class Mode : uint8_t
		{
			Edit,
			Simulate
		};

		Mode Current = Mode::Edit;

		[[nodiscard]] bool IsSimulating() const { return Current == Mode::Simulate; }
	};
}
