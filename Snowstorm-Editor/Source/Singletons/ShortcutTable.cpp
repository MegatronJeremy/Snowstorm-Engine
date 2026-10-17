#include "ShortcutTable.hpp"

#include <array>

namespace Snowstorm::Shortcuts
{
	namespace
	{
		constexpr Chord Plain(KeyCode key)
		{
			return {key, false, false};
		}
		constexpr Chord Ctrl(KeyCode key)
		{
			return {key, true, false};
		}
		constexpr Chord CtrlShift(KeyCode key)
		{
			return {key, true, true};
		}
		constexpr Chord Shift(KeyCode key)
		{
			return {key, false, true};
		}

		constexpr std::array<Row, 7> kCamera = {{
		    {"Right Mouse (hold)", "Look around / enable fly controls", {}},
		    {"W / A / S / D", "Move forward / left / back / right", Plain(Key::W)},
		    {"E / Q", "Move up / down", Plain(Key::E)},
		    {"Left Shift", "Sprint (move faster)", Plain(Key::LeftShift)},
		    {"Left Ctrl", "Move slower (precision)", Plain(Key::LeftControl)},
		    {"Mouse Wheel (RMB held)", "Adjust fly speed", {}},
		    {"Mouse Wheel (no RMB)", "Zoom / dolly (perspective) or zoom (ortho)", {}},
		}};

		constexpr std::array<Row, 5> kSelection = {{
		    {"Left Mouse (viewport)", "Select entity under cursor", {}},
		    {"Escape", "Clear selection (hides the gizmo)", Plain(Key::Escape)},
		    {"W", "Translate gizmo", Plain(Key::W)},
		    {"E", "Rotate gizmo", Plain(Key::E)},
		    {"R", "Scale gizmo", Plain(Key::R)},
		}};

		constexpr std::array<Row, 3> kFraming = {{
		    {"F", "Frame selected entity (or whole scene if none)", Plain(Key::F)},
		    {"Shift + F", "Frame the whole scene", Shift(Key::F)},
		    {"Double-click entity", "Focus camera on it (hierarchy or viewport)", {}},
		}};

		constexpr std::array<Row, 3> kEdit = {{
		    {"Ctrl + Z", "Undo", Ctrl(Key::Z)},
		    {"Ctrl + Y", "Redo", Ctrl(Key::Y)},
		    {"Ctrl + Shift + Z", "Redo (alternate)", CtrlShift(Key::Z)},
		}};

		constexpr std::array<Row, 4> kScene = {{
		    {"Ctrl + N", "New (empty) scene", Ctrl(Key::N)},
		    {"Ctrl + O", "Open scene...", Ctrl(Key::O)},
		    {"Ctrl + S", "Save current scene", Ctrl(Key::S)},
		    {"Ctrl + Shift + S", "Save scene as...", CtrlShift(Key::S)},
		}};

		constexpr std::array<Row, 2> kPlay = {{
		    {"Play button", "Run the scene (PIE) / Stop", {}},
		    {"Arrow next to Play", "Pick In Editor (PIE) or Standalone (launches Snowstorm-Runtime)", {}},
		}};

		constexpr std::array<Row, 1> kContent = {{
		    {"Drop files from the OS", "Import them into the project (models, textures, shaders, audio)", {}},
		}};

		constexpr std::array<Row, 4> kHierarchy = {{
		    {"Delete", "Delete the selected entity", Plain(Key::Delete)},
		    {"Right-click > Rename", "Rename the entity", {}},
		    {"Right-click > Duplicate", "Duplicate the entity", {}},
		    {"Right-click > Delete", "Delete the entity", {}},
		}};

		constexpr std::array<Row, 3> kDebug = {{
		    {"`  (backtick)", "Toggle the developer console", Plain(Key::GraveAccent)},
		    {"Tab (in console)", "Autocomplete command / CVar (repeat to cycle)", Plain(Key::Tab)},
		    {"Up / Down (in console)", "Cycle command history", Plain(Key::Up)},
		}};

		constexpr std::array<Section, 9> kSections = {{
		    {"cam", "Camera (hold Right Mouse Button in viewport)", kCamera},
		    {"sel", "Selection & Gizmos", kSelection},
		    {"frame", "Camera Framing", kFraming},
		    {"edit", "Edit", kEdit},
		    {"scene", "Scene", kScene},
		    {"play", "Play (viewport toolbar)", kPlay},
		    {"content", "Content Browser", kContent},
		    {"hier", "Scene Hierarchy", kHierarchy},
		    {"debug", "Debug / Console", kDebug},
		}};
	}

	std::span<const Section> Sections()
	{
		return kSections;
	}
}
