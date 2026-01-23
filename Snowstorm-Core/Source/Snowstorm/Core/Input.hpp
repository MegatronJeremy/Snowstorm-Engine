#pragma once

#include "Snowstorm/Core/KeyCodes.hpp"
#include "Snowstorm/Core/MouseCodes.hpp"

#include <utility>

namespace Snowstorm
{
	enum class CursorMode
	{
		Normal, // Visible and free
		Locked // Hidden and locked in place
	};

	class Input
	{
	public:
		static bool IsKeyPressed(KeyCode key);
		static bool IsMouseButtonPressed(MouseCode button);
		static std::pair<float, float> GetMousePosition();
		static float GetMouseX();
		static float GetMouseY();

		static std::pair<float, float> GetMouseDelta();
		static void UpdateMousePosition();

	private:
		inline static std::pair<float, float> s_LastMousePosition{};
		inline static std::pair<float, float> s_MouseDelta{};
	};
}
