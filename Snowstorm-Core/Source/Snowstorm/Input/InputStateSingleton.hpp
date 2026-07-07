#pragma once

#include "Snowstorm/ECS/Singleton.hpp"

#include <glm/vec2.hpp>
#include <bitset>
#include <cstdint>

namespace Snowstorm
{
	struct InputStateSingleton final : Singleton
	{
		// Call once per frame at the end of Update.
		void Clear()
		{
			MouseDelta = {0.0f, 0.0f};
			ScrollDelta = {0.0f, 0.0f};

			PressedThisFrame.reset();
			ReleasedThisFrame.reset();

			MousePressedThisFrame.reset();
			MouseReleasedThisFrame.reset();
		}

		// ---- Keyboard ----
		static constexpr uint32_t MaxKeys = 512;

		std::bitset<MaxKeys> Down{};
		std::bitset<MaxKeys> PressedThisFrame{};
		std::bitset<MaxKeys> ReleasedThisFrame{};

		// ---- Mouse ----
		static constexpr uint32_t MaxMouseButtons = 16;

		std::bitset<MaxMouseButtons> MouseDown{};
		std::bitset<MaxMouseButtons> MousePressedThisFrame{};
		std::bitset<MaxMouseButtons> MouseReleasedThisFrame{};

		glm::vec2 MousePos{0.0f, 0.0f};
		glm::vec2 MouseDelta{0.0f, 0.0f};
		glm::vec2 ScrollDelta{0.0f, 0.0f};

		// Optional: window focus gating
		bool WindowFocused = true;

		// UI capture, published from ImGui each frame by the editor (Core stays ImGui-free).
		//   WantCaptureMouse    - some ImGui widget wants the mouse. NOTE: the viewport is itself an ImGui
		//                         window (scene drawn into an Image), so this is TRUE over the viewport —
		//                         do NOT use it to gate viewport/camera input; use the viewport's own focus.
		//   WantCaptureKeyboard - ImGui wants the keyboard, INCLUDING keyboard-nav of a focused window (so
		//                         it's true just from clicking the viewport). Too broad for shortcut gating.
		//   WantTextInput       - a TEXT FIELD is active. This is the precise "user is typing" signal that
		//                         keyboard shortcuts (F, Ctrl+S, gizmo keys, console toggle) should gate on,
		//                         so they still work over a focused viewport but not while editing text.
		bool WantCaptureMouse = false;
		bool WantCaptureKeyboard = false;
		bool WantTextInput = false;
	};
}
