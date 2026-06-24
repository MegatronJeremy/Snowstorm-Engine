#pragma once

namespace Snowstorm::EditorTheme
{
	// ---- NERV-style chrome helpers (call inside an ImGui window) ----

	// A bold section header rendered as "// LABEL" in the amber accent, followed by a separator.
	// Use at the top of a panel to give it the terminal look.
	void SectionHeader(const char* label);

	// A red NERV-style warning banner: "■ <text>" on a dim red bar. For alerts/empty states.
	void WarningBanner(const char* text);

	// Apply the NERV/Evangelion-inspired ImGui style: near-black backgrounds, amber accent,
	// sharp corners, hard borders, dense spacing. Call after ImGui::CreateContext().
	void ApplyEvangelion();

	// Load a monospace font from Assets/Fonts if one is present, making it the default.
	// No-ops (keeping the built-in font) when no font file is found, so the repo builds and
	// runs without shipping a TTF. Call after the context exists and before the first frame.
	void LoadMonospaceFont();
}
