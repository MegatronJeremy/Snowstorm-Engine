#pragma once

#include <utility>

#include "Snowstorm/Input/InputStateSingleton.hpp"
#include "Snowstorm/Events/EventBus.hpp"
#include "Snowstorm/Events/KeyEvent.hpp"
#include "Snowstorm/Events/MouseEvent.hpp"

namespace Snowstorm
{
	// Own one per World (or per Application if you have only one active world).
	// It subscribes to EventBus and updates InputStateSingleton in that World.
	class InputEventBridge
	{
	public:
		InputEventBridge(EventBus& bus, InputStateSingleton& input)
			: m_Input(&input)
		{
			// Keyboard
			m_KeyDown = bus.Subscribe<KeyPressedEvent>([this](const KeyPressedEvent& e)
			{
				if (const int key = e.m_KeyCode; key >= 0 && std::cmp_less(key, InputStateSingleton::MaxKeys))
				{
					if (!m_Input->Down.test(key))
						m_Input->PressedThisFrame.set(key);

					m_Input->Down.set(key);
				}
				return false;
			}, 0);

			m_KeyUp = bus.Subscribe<KeyReleasedEvent>([this](const KeyReleasedEvent& e)
			{
				if (const int key = e.m_KeyCode; key >= 0 && std::cmp_less(key, InputStateSingleton::MaxKeys))
				{
					m_Input->Down.reset(key);
					m_Input->ReleasedThisFrame.set(key);
				}
				return false;
			}, 0);

			// Mouse move
			m_MouseMove = bus.Subscribe<MouseMovedEvent>([this](const MouseMovedEvent& e)
			{
				const glm::vec2 newPos{e.mouseX, e.mouseY};
				m_Input->MouseDelta += (newPos - m_Input->MousePos);
				m_Input->MousePos = newPos;
				return false;
			}, 0);

			// Scroll
			m_Scroll = bus.Subscribe<MouseScrolledEvent>([this](const MouseScrolledEvent& e)
			{
				m_Input->ScrollDelta += glm::vec2{e.xOffset, e.yOffset};
				return false;
			}, 0);

			// Mouse buttons
			m_MouseDown = bus.Subscribe<MouseButtonPressedEvent>([this](const MouseButtonPressedEvent& e)
			{
				if (const int b = e.m_Button; b >= 0 && std::cmp_less(b, InputStateSingleton::MaxMouseButtons))
				{
					if (!m_Input->MouseDown.test(b))
					{
						m_Input->MousePressedThisFrame.set(b);
					}

					m_Input->MouseDown.set(b);
				}
				return false;
			}, 0);

			m_MouseUp = bus.Subscribe<MouseButtonReleasedEvent>([this](const MouseButtonReleasedEvent& e)
			{
				if (const int b = e.m_Button; b >= 0 && std::cmp_less(b, InputStateSingleton::MaxMouseButtons))
				{
					m_Input->MouseDown.reset(b);
					m_Input->MouseReleasedThisFrame.set(b);
				}
				return false;
			}, 0);

			// Window focus (optional)
			// If you add WindowFocusEvent/WindowLostFocusEvent later, subscribe here.
			// For now you already have those enums but not event structs.
		}

	private:
		InputStateSingleton* m_Input = nullptr;

		EventBus::Connection m_KeyDown;
		EventBus::Connection m_KeyUp;
		EventBus::Connection m_MouseMove;
		EventBus::Connection m_Scroll;
		EventBus::Connection m_MouseDown;
		EventBus::Connection m_MouseUp;
	};
}
