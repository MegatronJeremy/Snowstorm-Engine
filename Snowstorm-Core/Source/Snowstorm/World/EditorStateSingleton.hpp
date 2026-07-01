#pragma once

#include "Snowstorm/ECS/Singleton.hpp"

namespace Snowstorm
{
	// Editor-wide Edit/Play state. In Edit (the default) the scene is being authored: simulation systems
	// (scripts, rotators — anything that returns RunsInEditMode() == false) are skipped, so the authored
	// scene doesn't move under the cursor and the gizmo doesn't fight an animation. In Play the whole
	// system set runs so the world simulates. "Play" is the umbrella for every non-Edit mode: today it's
	// the in-editor variant where the EDITOR camera + selection stay live (Unreal would call this
	// Simulate), and a future true Play-In-Editor (possess a player, #68) or Standalone (#67) are further
	// play modes. This is also the gate the temporal-upscaling work needs: motion (jitter / history
	// accumulation) is tied to Play.
	//
	// Editor-only: a packaged runtime never creates this singleton, and SystemManager treats "no singleton"
	// as "everything runs", so the gate is a no-op outside the editor.
	class EditorStateSingleton final : public Singleton
	{
	public:
		enum class Mode : uint8_t
		{
			Edit,
			Play
		};

		Mode Current = Mode::Edit;

		[[nodiscard]] bool IsPlaying() const { return Current == Mode::Play; }
	};
}
