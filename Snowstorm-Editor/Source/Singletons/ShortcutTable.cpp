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

		constexpr std::array<Row, 5> kView2D = {{
		    {"2 (viewport) / 2D button", "Toggle the 2D Scene view (opens on its own for a Canvas2D scene)", Plain(Key::D2)},
		    {"Middle or Right Mouse (drag)", "Pan (2D view)", {}},
		    {"Mouse Wheel", "Zoom about the cursor (2D view)", {}},
		    {"Right Mouse + W / A / S / D", "Pan with the keyboard (2D view)", {}},
		    {"F / Shift + F", "Frame the selected sprite / every sprite (2D view)", {}},
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

		constexpr std::array<Row, 4> kPossess = {{
		    {"Escape", "Release input to the editor; Escape again stops", {}},
		    {"Shift + F1", "Release input to the editor", {}},
		    {"Left Mouse (viewport)", "Take input back after a release", {}},
		    {"While the game holds input", "Editor shortcuts and the fly camera are off", {}},
		}};

		constexpr std::array<Row, 4> kScene = {{
		    {"Ctrl + N", "New (empty) scene", Ctrl(Key::N)},
		    {"Ctrl + O", "Open scene...", Ctrl(Key::O)},
		    {"Ctrl + S", "Save current scene", Ctrl(Key::S)},
		    {"Ctrl + Shift + S", "Save scene as...", CtrlShift(Key::S)},
		}};

		constexpr std::array<Row, 2> kPlay = {{
		    {"Play button", "Run the scene (Simulate) / Stop", {}},
		    {"Arrow next to Play", "Pick In Editor (Simulate) or Standalone (launches Snowstorm-Runtime)", {}},
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

		constexpr std::array<Section, 11> kSections = {{
		    {"cam", "Camera (hold Right Mouse Button in viewport)", kCamera},
		    {"view2d", "2D Scene view", kView2D},
		    {"sel", "Selection & Gizmos", kSelection},
		    {"possess", "Play (Possess)", kPossess},
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
