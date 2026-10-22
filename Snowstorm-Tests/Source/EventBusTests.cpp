#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Events/ApplicationEvent.hpp"
#include "Snowstorm/Events/EventBus.hpp"

#include <utility>
#include <vector>

using namespace Snowstorm;

TEST_CASE("EventBus runs handlers for their event type only", "[events]")
{
	EventBus bus;
	int closes = 0;
	int ticks = 0;
	const auto a = bus.Subscribe<WindowCloseEvent>([&](WindowCloseEvent&)
	                                               { ++closes; return false; });
	const auto b = bus.Subscribe<AppTickEvent>([&](AppTickEvent&)
	                                           { ++ticks; return false; });

	WindowCloseEvent close;
	bus.Publish(close);

	REQUIRE(closes == 1);
	REQUIRE(ticks == 0);
}

TEST_CASE("EventBus passes the typed event to the handler", "[events]")
{
	EventBus bus;
	uint32_t w = 0;
	uint32_t h = 0;
	const auto c = bus.Subscribe<WindowResizeEvent>([&](WindowResizeEvent& e)
	                                                { w = e.Width; h = e.Height; return false; });

	WindowResizeEvent resize(640, 480);
	bus.Publish(resize);

	REQUIRE(w == 640);
	REQUIRE(h == 480);
}

TEST_CASE("EventBus publishing with no subscribers is a no-op", "[events]")
{
	EventBus bus;
	AppTickEvent tick;
	bus.Publish(tick);
	REQUIRE_FALSE(tick.Handled);
}

TEST_CASE("EventBus runs handlers by descending priority", "[events]")
{
	EventBus bus;
	std::vector<int> order;
	const auto lo = bus.Subscribe<AppTickEvent>([&](AppTickEvent&)
	                                            { order.push_back(-1); return false; },
	                                            -1);
	const auto hi = bus.Subscribe<AppTickEvent>([&](AppTickEvent&)
	                                            { order.push_back(5); return false; },
	                                            5);
	const auto mid = bus.Subscribe<AppTickEvent>([&](AppTickEvent&)
	                                             { order.push_back(0); return false; });

	AppTickEvent tick;
	bus.Publish(tick);

	REQUIRE(order == std::vector<int>{5, 0, -1});
}

TEST_CASE("EventBus: a handler returning true marks the event handled and stops later handlers", "[events]")
{
	EventBus bus;
	int later = 0;
	const auto first = bus.Subscribe<AppTickEvent>([](AppTickEvent&)
	                                               { return true; },
	                                               10);
	const auto second = bus.Subscribe<AppTickEvent>([&](AppTickEvent&)
	                                                { ++later; return false; },
	                                                0);

	AppTickEvent tick;
	bus.Publish(tick);

	REQUIRE(tick.Handled);
	REQUIRE(later == 0);
}

TEST_CASE("EventBus: an event already Handled reaches no handler", "[events]")
{
	EventBus bus;
	int calls = 0;
	const auto c = bus.Subscribe<AppTickEvent>([&](AppTickEvent&)
	                                           { ++calls; return false; });

	AppTickEvent tick;
	tick.Handled = true;
	bus.Publish(tick);

	REQUIRE(calls == 0);
}

TEST_CASE("EventBus: a handler returning false leaves the event unhandled and propagating", "[events]")
{
	EventBus bus;
	int calls = 0;
	const auto a = bus.Subscribe<AppTickEvent>([&](AppTickEvent&)
	                                           { ++calls; return false; });
	const auto b = bus.Subscribe<AppTickEvent>([&](AppTickEvent&)
	                                           { ++calls; return false; });

	AppTickEvent tick;
	bus.Publish(tick);

	REQUIRE(calls == 2);
	REQUIRE_FALSE(tick.Handled);
}

TEST_CASE("EventBus::Connection::Disconnect unsubscribes and is idempotent", "[events]")
{
	EventBus bus;
	int calls = 0;
	auto c = bus.Subscribe<AppTickEvent>([&](AppTickEvent&)
	                                     { ++calls; return false; });

	c.Disconnect();
	c.Disconnect();

	AppTickEvent tick;
	bus.Publish(tick);

	REQUIRE(calls == 0);
	REQUIRE(c.Bus == nullptr);
	REQUIRE(c.Type == EventType::None);
	REQUIRE(c.Id == 0);
}

TEST_CASE("EventBus::Connection destructor unsubscribes", "[events]")
{
	EventBus bus;
	int calls = 0;
	{
		auto c = bus.Subscribe<AppTickEvent>([&](AppTickEvent&)
		                                     { ++calls; return false; });
		AppTickEvent tick;
		bus.Publish(tick);
		REQUIRE(calls == 1);
	}

	AppTickEvent tick;
	bus.Publish(tick);
	REQUIRE(calls == 1);
}

TEST_CASE("EventBus: disconnecting one handler leaves the others subscribed", "[events]")
{
	EventBus bus;
	int a = 0;
	int b = 0;
	auto ca = bus.Subscribe<AppTickEvent>([&](AppTickEvent&)
	                                      { ++a; return false; });
	const auto cb = bus.Subscribe<AppTickEvent>([&](AppTickEvent&)
	                                            { ++b; return false; });

	ca.Disconnect();
	AppTickEvent tick;
	bus.Publish(tick);

	REQUIRE(a == 0);
	REQUIRE(b == 1);
}

TEST_CASE("EventBus::Connection move construction transfers ownership and leaves the source inert", "[events]")
{
	EventBus bus;
	int calls = 0;
	auto src = bus.Subscribe<AppTickEvent>([&](AppTickEvent&)
	                                       { ++calls; return false; });

	{
		EventBus::Connection dst(std::move(src));
		REQUIRE(src.Bus == nullptr);
		REQUIRE(src.Type == EventType::None);
		REQUIRE(src.Id == 0);

		src.Disconnect(); // inert: must not unsubscribe the moved handler
		AppTickEvent tick;
		bus.Publish(tick);
		REQUIRE(calls == 1);
	}

	AppTickEvent tick;
	bus.Publish(tick);
	REQUIRE(calls == 1); // the destination's destructor unsubscribed
}

TEST_CASE("EventBus::Connection move assignment disconnects the old handler and takes the new one", "[events]")
{
	EventBus bus;
	int oldCalls = 0;
	int newCalls = 0;
	auto dst = bus.Subscribe<AppTickEvent>([&](AppTickEvent&)
	                                       { ++oldCalls; return false; });
	auto src = bus.Subscribe<WindowCloseEvent>([&](WindowCloseEvent&)
	                                           { ++newCalls; return false; });

	dst = std::move(src);

	AppTickEvent tick;
	bus.Publish(tick);
	WindowCloseEvent close;
	bus.Publish(close);

	REQUIRE(oldCalls == 0);
	REQUIRE(newCalls == 1);
	REQUIRE(src.Bus == nullptr);
	REQUIRE(dst.Type == EventType::WindowClose);
}

TEST_CASE("EventBus::Connection default-constructed is inert", "[events]")
{
	EventBus::Connection c;
	c.Disconnect();
	REQUIRE(c.Bus == nullptr);
}
