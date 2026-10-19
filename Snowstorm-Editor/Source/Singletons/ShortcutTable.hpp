#pragma once

#include "Snowstorm/Core/KeyCodes.hpp"

#include <span>

namespace Snowstorm::Shortcuts
{
	// A keyboard chord. Key 0 marks a row with no single keyboard chord (mouse gestures, menus, toolbar).
	struct Chord
	{
		KeyCode Key = 0;
		bool Ctrl = false;
		bool Shift = false;

		bool operator==(const Chord&) const = default;
	};

	struct Row
	{
		const char* Keys;
		const char* Action;
		Shortcuts::Chord Binding;
	};

	// One ImGui table. Chords must be unique within a section; the same key may recur across sections
	// that apply in different contexts (W moves the camera under RMB and picks the translate gizmo without).
	struct Section
	{
		const char* TableId;
		const char* Title;
		std::span<const Row> Rows;
	};

	// The Help > Keyboard & Mouse Shortcuts window reads this and nothing else.
	std::span<const Section> Sections();
}
