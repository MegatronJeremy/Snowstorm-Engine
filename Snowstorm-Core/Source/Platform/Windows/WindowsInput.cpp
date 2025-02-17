#include "pch.h"
#include "Snowstorm/Core/Input.hpp"
#include "Snowstorm/Core/Application.hpp"

#include <GLFW/glfw3.h>

namespace Snowstorm
{
	bool Input::IsKeyPressed(const KeyCode key)
	{
		const auto window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());

		// tells us if a key is actually down
		const auto state = glfwGetKey(window, key);
		return state == GLFW_PRESS || state == GLFW_REPEAT;
	}

	bool Input::IsMouseButtonPressed(const MouseCode button)
	{
		const auto window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());

		const auto state = glfwGetMouseButton(window, button);
		return state == GLFW_PRESS;
	}

	std::pair<float, float> Input::GetMousePosition()
	{
		const auto window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		double xPos, yPos;

		glfwGetCursorPos(window, &xPos, &yPos);

		return {static_cast<float>(xPos), static_cast<float>(yPos)};
	}

	float Input::GetMouseX()
	{
		auto [x, y] = GetMousePosition();
		return x;
	}

	float Input::GetMouseY()
	{
		auto [x, y] = GetMousePosition();
		return y;
	}

	std::pair<float, float> Input::GetMouseDelta()
	{
		return s_MouseDelta;
	}

	void Input::UpdateMousePosition()
	{
		const std::pair currentMousePos{GetMouseX(), GetMouseY()};
		s_MouseDelta.first -= currentMousePos.first;
		s_MouseDelta.second -= currentMousePos.second;

		s_LastMousePosition = currentMousePos;
	}

	void Input::SetCursorMode(const CursorMode mode)
	{
		const auto window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		if (mode == CursorMode::Locked)
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		else
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
}
