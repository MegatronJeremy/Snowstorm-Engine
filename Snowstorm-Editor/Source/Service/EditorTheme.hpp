#pragma once

namespace Snowstorm::EditorTheme
{
	// ---- NERV-style chrome helpers (call inside an ImGui window) ----

	// A bold section header rendered as "// LABEL" in the amber accent, followed by a separator.
	// Use at the top of a panel to give it the terminal look.
	void SectionHeader(const char* label);

	// A full-width amber PRIMARY action button (the NERV command-console look): bright amber fill,
	// dark text, uppercased label. `width` <= 0 stretches to the content region. Returns true when
	// clicked. For the one prominent action per panel (e.g. "+ CREATE"); ordinary buttons stay default.
	bool PrimaryButton(const char* label, float width = -1.0f);

	// The theme's amber accent as a packed ImU32 (for draw-list glyphs that should match the accent).
	unsigned int AccentColor();

	// A red NERV-style warning banner: "■ <text>" on a dim red bar. For alerts/empty states.
	void WarningBanner(const char* text);

	// Apply the NERV/Evangelion-inspired ImGui style: near-black backgrounds, amber accent,
	// sharp corners, hard borders, dense spacing. Call after ImGui::CreateContext().
	void ApplyEvangelion();

	// Load a monospace font from Engine/Fonts if one is present, making it the default.
	// No-ops (keeping the built-in font) when no font file is found, so the repo builds and
	// runs without shipping a TTF. Call after the context exists and before the first frame.
	void LoadMonospaceFont();
}
