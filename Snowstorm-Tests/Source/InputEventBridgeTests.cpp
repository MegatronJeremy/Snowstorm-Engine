#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Events/EventBus.hpp"
#include "Snowstorm/Events/KeyEvent.hpp"
#include "Snowstorm/Events/MouseEvent.hpp"
#include "Snowstorm/Input/InputEventBridge.hpp"

using namespace Snowstorm;

namespace
{
	template <typename E>
	void Send(EventBus& bus, E e)
	{
		bus.Publish(e);
	}
}

TEST_CASE("InputEventBridge key press, repeat and release", "[input]")
{
	EventBus bus;
	InputStateSingleton input;
	InputEventBridge bridge(bus, input);

	Send(bus, KeyPressedEvent(65, 0));
	REQUIRE(input.Down.test(65));
	REQUIRE(input.PressedThisFrame.test(65));

	input.Clear();
	REQUIRE(input.Down.test(65));
	REQUIRE_FALSE(input.PressedThisFrame.test(65));

	// An OS auto-repeat of a held key is not a new press.
	Send(bus, KeyPressedEvent(65, 1));
	REQUIRE(input.Down.test(65));
	REQUIRE_FALSE(input.PressedThisFrame.test(65));

	Send(bus, KeyReleasedEvent(65));
	REQUIRE_FALSE(input.Down.test(65));
	REQUIRE(input.ReleasedThisFrame.test(65));
	REQUIRE_FALSE(input.PressedThisFrame.test(65));
}

TEST_CASE("InputEventBridge ignores key codes outside the table", "[input]")
{
	EventBus bus;
	InputStateSingleton input;
	InputEventBridge bridge(bus, input);

	Send(bus, KeyPressedEvent(-1, 0));
	Send(bus, KeyPressedEvent(static_cast<int>(InputStateSingleton::MaxKeys), 0));
	Send(bus, KeyReleasedEvent(-1));
	Send(bus, KeyReleasedEvent(static_cast<int>(InputStateSingleton::MaxKeys)));
	REQUIRE(input.Down.none());
	REQUIRE(input.PressedThisFrame.none());
	REQUIRE(input.ReleasedThisFrame.none());

	const int last = static_cast<int>(InputStateSingleton::MaxKeys) - 1;
	Send(bus, KeyPressedEvent(last, 0));
	REQUIRE(input.Down.test(static_cast<size_t>(last)));
}

TEST_CASE("InputEventBridge mouse buttons", "[input]")
{
	EventBus bus;
	InputStateSingleton input;
	InputEventBridge bridge(bus, input);

	Send(bus, MouseButtonPressedEvent(1));
	REQUIRE(input.MouseDown.test(1));
	REQUIRE(input.MousePressedThisFrame.test(1));

	input.Clear();
	Send(bus, MouseButtonPressedEvent(1));
	REQUIRE(input.MouseDown.test(1));
	REQUIRE_FALSE(input.MousePressedThisFrame.test(1));

	Send(bus, MouseButtonReleasedEvent(1));
	REQUIRE_FALSE(input.MouseDown.test(1));
	REQUIRE(input.MouseReleasedThisFrame.test(1));

	Send(bus, MouseButtonPressedEvent(-1));
	Send(bus, MouseButtonPressedEvent(static_cast<int>(InputStateSingleton::MaxMouseButtons)));
	REQUIRE(input.MouseDown.none());
}

TEST_CASE("InputEventBridge accumulates scroll and resets it per frame", "[input]")
{
	EventBus bus;
	InputStateSingleton input;
	InputEventBridge bridge(bus, input);

	Send(bus, MouseScrolledEvent(1.0f, 2.0f));
	Send(bus, MouseScrolledEvent(0.5f, -0.5f));
	REQUIRE(input.ScrollDelta.x == 1.5f);
	REQUIRE(input.ScrollDelta.y == 1.5f);

	input.Clear();
	REQUIRE(input.ScrollDelta.x == 0.0f);
	REQUIRE(input.ScrollDelta.y == 0.0f);
}

TEST_CASE("InputEventBridge cursor delta accumulates within a frame and the position persists", "[input]")
{
	EventBus bus;
	InputStateSingleton input;
	InputEventBridge bridge(bus, input);

	Send(bus, MouseMovedEvent(10.0f, 20.0f));
	REQUIRE(input.MousePos == glm::vec2{10.0f, 20.0f});
	REQUIRE(input.MouseDelta == glm::vec2{10.0f, 20.0f});

	input.Clear();
	Send(bus, MouseMovedEvent(13.0f, 18.0f));
	Send(bus, MouseMovedEvent(14.0f, 18.0f));
	REQUIRE(input.MousePos == glm::vec2{14.0f, 18.0f});
	REQUIRE(input.MouseDelta == glm::vec2{4.0f, -2.0f});

	input.Clear();
	REQUIRE(input.MouseDelta == glm::vec2{0.0f, 0.0f});
	REQUIRE(input.MousePos == glm::vec2{14.0f, 18.0f});
}

TEST_CASE("InputEventBridge does not apply an event a higher-priority handler consumed", "[input]")
{
	EventBus bus;
	InputStateSingleton input;
	InputEventBridge bridge(bus, input);

	int consumed = 0;
	const auto guard = bus.Subscribe<MouseScrolledEvent>([&](const MouseScrolledEvent&)
	                                                     {
		++consumed;
		return true; }, 10);

	Send(bus, MouseScrolledEvent(3.0f, 3.0f));
	REQUIRE(consumed == 1);
	REQUIRE(input.ScrollDelta == glm::vec2{0.0f, 0.0f});
}

TEST_CASE("InputEventBridge skips an event already marked Handled", "[input]")
{
	EventBus bus;
	InputStateSingleton input;
	InputEventBridge bridge(bus, input);

	KeyPressedEvent e(65, 0);
	e.Handled = true;
	bus.Publish(e);
	REQUIRE(input.Down.none());
	REQUIRE(input.PressedThisFrame.none());
}

TEST_CASE("InputEventBridge stops updating state once destroyed", "[input]")
{
	EventBus bus;
	InputStateSingleton input;
	{
		InputEventBridge bridge(bus, input);
		Send(bus, KeyPressedEvent(65, 0));
		REQUIRE(input.Down.test(65));
	}
	Send(bus, KeyPressedEvent(66, 0));
	REQUIRE_FALSE(input.Down.test(66));
}
