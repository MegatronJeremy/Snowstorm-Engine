#pragma once

#include "Event.hpp"

namespace Snowstorm
{
	struct MouseMovedEvent final : Event
	{
		MouseMovedEvent(const float x, const float y)
			: mouseX(x), mouseY(y)
		{
		}

		[[nodiscard]] std::string ToString() const override
		{
			std::stringstream ss;
			ss << "MouseMovedEvent: " << mouseX << ", " << mouseY;
			return ss.str();
		}

		EVENT_CLASS_TYPE(MouseMoved)
		EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)

		float mouseX, mouseY;
	};

	class MouseScrolledEvent final : public Event
	{
	public:
		MouseScrolledEvent(const float xOffset, const float yOffset)
			: xOffset(xOffset), yOffset(yOffset)
		{
		}

		[[nodiscard]] std::string ToString() const override
		{
			std::stringstream ss;
			ss << "MouseScrolledEvent: " << xOffset << ", " << yOffset;
			return ss.str();
		}

		EVENT_CLASS_TYPE(MouseScrolled)
		EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)

		float xOffset, yOffset;
	};

	struct MouseButtonEvent : Event
	{
		EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)

		int m_Button;

	protected:
		explicit MouseButtonEvent(const int button)
			: m_Button(button)
		{
		}
	};

	struct MouseButtonPressedEvent final : MouseButtonEvent
	{
		explicit MouseButtonPressedEvent(const int button)
			: MouseButtonEvent(button)
		{
		}

		[[nodiscard]] std::string ToString() const override
		{
			std::stringstream ss;
			ss << "MouseButtonPressedEvent: " << m_Button;
			return ss.str();
		}

		EVENT_CLASS_TYPE(MouseButtonPressed)
	};

	struct MouseButtonReleasedEvent final : MouseButtonEvent
	{
		explicit MouseButtonReleasedEvent(const int button)
			: MouseButtonEvent(button)
		{
		}

		[[nodiscard]] std::string ToString() const override
		{
			std::stringstream ss;
			ss << "MouseButtonReleasedEvent: " << m_Button;
			return ss.str();
		}

		EVENT_CLASS_TYPE(MouseButtonReleased)
	};
}
