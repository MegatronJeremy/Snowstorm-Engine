#pragma once

namespace Snowstorm
{
	// Who owns keyboard and mouse during possessed play (#68). Unreal's PIE model: the viewport takes
	// game input on Play, Shift+F1 hands the cursor back to the editor, Esc releases first and stops second.
	struct PlayInputStep
	{
		bool Captured = false;
		bool Stop = false;
	};

	struct PlayInputEvents
	{
		bool Escape = false;
		bool ReleaseChord = false;
		bool ViewportClicked = false;
		bool WantTextInput = false;
	};

	[[nodiscard]] constexpr PlayInputStep StepPlayInput(const bool possessing, const bool captured,
	                                                    const PlayInputEvents& ev)
	{
		if (!possessing)
		{
			return {};
		}
		if (ev.WantTextInput)
		{
			return {captured, false};
		}
		if (captured)
		{
			return {!(ev.Escape || ev.ReleaseChord), false};
		}
		if (ev.Escape)
		{
			return {false, true};
		}
		return {ev.ViewportClicked, false};
	}

	// Editor-only key bindings (shortcuts, gizmo keys, framing, delete) stand down while the game holds input.
	[[nodiscard]] constexpr bool EditorKeysBlocked(const bool wantTextInput, const bool gameCaptured)
	{
		return wantTextInput || gameCaptured;
	}
}
