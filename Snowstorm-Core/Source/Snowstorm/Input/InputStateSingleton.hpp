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

		// Optional: UI capture (ImGui etc). If true, gameplay/editor camera systems should ignore input.
		bool WantCaptureMouse = false;
		bool WantCaptureKeyboard = false;
	};
}
