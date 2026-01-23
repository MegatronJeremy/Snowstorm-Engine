#pragma once

#include "Event.hpp"

namespace Snowstorm
{
	struct KeyEvent : Event // basically an abstract event
	{
		EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

		int m_KeyCode;

	protected:
		explicit KeyEvent(const int keycode)
			: m_KeyCode(keycode)
		{
		}
	};

	struct KeyPressedEvent final : KeyEvent
	{
		KeyPressedEvent(const int keycode, const int repeatCount)
			: KeyEvent(keycode), m_RepeatCount(repeatCount)
		{
		}

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "KeyPressedEvent: " << m_KeyCode << " (" << m_RepeatCount << " repeats)";
			return ss.str();
		}

		EVENT_CLASS_TYPE(KeyPressed)

		int m_RepeatCount;
	};

	struct KeyReleasedEvent final : KeyEvent
	{
		explicit KeyReleasedEvent(const int keycode):
			KeyEvent(keycode)
		{
		}

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "KeyReleasedEvent: " << m_KeyCode;
			return ss.str();
		}

		EVENT_CLASS_TYPE(KeyReleased)
	};

	struct KeyTypedEvent final : KeyEvent
	{
		explicit KeyTypedEvent(const int keycode)
			: KeyEvent(keycode)
		{
		}

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "KeyTypedEvent: " << m_KeyCode;
			return ss.str();
		}

		EVENT_CLASS_TYPE(KeyTyped)
	};
}
