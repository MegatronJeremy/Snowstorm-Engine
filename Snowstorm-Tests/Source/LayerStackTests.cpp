#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Core/LayerStack.hpp"

#include <memory>
#include <string>
#include <vector>

using namespace Snowstorm;

namespace
{
	using EventLog = std::vector<std::string>;

	class RecordingLayer final : public Layer
	{
	public:
		RecordingLayer(const std::string& name, EventLog& log) : Layer(name), m_Log(log)
		{
		}

		~RecordingLayer() override { m_Log.push_back("delete " + GetName()); }

		void OnDetach() override { m_Log.push_back("detach " + GetName()); }

	private:
		EventLog& m_Log;
	};

	std::vector<std::string> Names(const LayerStack& stack)
	{
		std::vector<std::string> names;
		for (const Layer* layer : stack)
			names.push_back(layer->GetName());
		return names;
	}

	using Names_t = std::vector<std::string>;
}

TEST_CASE("PushLayer keeps push order below every overlay", "[core][layerstack]")
{
	EventLog log;
	LayerStack stack;
	stack.PushLayer(new RecordingLayer("L0", log));
	stack.PushOverlay(new RecordingLayer("O0", log));
	stack.PushLayer(new RecordingLayer("L1", log));
	stack.PushOverlay(new RecordingLayer("O1", log));
	stack.PushLayer(new RecordingLayer("L2", log));

	CHECK(Names(stack) == Names_t{"L0", "L1", "L2", "O0", "O1"});
}

TEST_CASE("Reverse iteration visits overlays first", "[core][layerstack]")
{
	EventLog log;
	LayerStack stack;
	stack.PushLayer(new RecordingLayer("L0", log));
	stack.PushOverlay(new RecordingLayer("O0", log));
	stack.PushLayer(new RecordingLayer("L1", log));

	Names_t reversed;
	for (auto it = stack.rbegin(); it != stack.rend(); ++it)
		reversed.push_back((*it)->GetName());
	CHECK(reversed == Names_t{"O0", "L1", "L0"});

	const LayerStack& constStack = stack;
	Names_t constReversed;
	for (auto it = constStack.rbegin(); it != constStack.rend(); ++it)
		constReversed.push_back((*it)->GetName());
	CHECK(constReversed == reversed);
}

TEST_CASE("PopLayer removes the layer, detaches it and leaves ownership with the caller", "[core][layerstack]")
{
	EventLog log;
	LayerStack stack;
	auto* l0 = new RecordingLayer("L0", log);
	auto* l1 = new RecordingLayer("L1", log);
	stack.PushLayer(l0);
	stack.PushLayer(l1);
	stack.PushOverlay(new RecordingLayer("O0", log));

	stack.PopLayer(l0);
	std::unique_ptr<Layer> owned(l0);

	CHECK(Names(stack) == Names_t{"L1", "O0"});
	CHECK(log == EventLog{"detach L0"});
}

TEST_CASE("PopLayer keeps the insert index consistent for later pushes", "[core][layerstack]")
{
	EventLog log;
	LayerStack stack;
	auto* l0 = new RecordingLayer("L0", log);
	stack.PushLayer(l0);
	stack.PushLayer(new RecordingLayer("L1", log));
	stack.PushOverlay(new RecordingLayer("O0", log));

	stack.PopLayer(l0);
	std::unique_ptr<Layer> owned(l0);
	stack.PushLayer(new RecordingLayer("L2", log));

	CHECK(Names(stack) == Names_t{"L1", "L2", "O0"});
}

TEST_CASE("PopOverlay removes the overlay without moving the layer boundary", "[core][layerstack]")
{
	EventLog log;
	LayerStack stack;
	stack.PushLayer(new RecordingLayer("L0", log));
	auto* o0 = new RecordingLayer("O0", log);
	stack.PushOverlay(o0);
	stack.PushOverlay(new RecordingLayer("O1", log));

	stack.PopOverlay(o0);
	std::unique_ptr<Layer> owned(o0);
	CHECK(Names(stack) == Names_t{"L0", "O1"});
	CHECK(log == EventLog{"detach O0"});

	stack.PushLayer(new RecordingLayer("L1", log));
	CHECK(Names(stack) == Names_t{"L0", "L1", "O1"});
}

TEST_CASE("Popping a layer that is not in the stack changes nothing", "[core][layerstack]")
{
	EventLog log;
	LayerStack stack;
	stack.PushLayer(new RecordingLayer("L0", log));
	stack.PushOverlay(new RecordingLayer("O0", log));

	RecordingLayer stranger("X", log);
	stack.PopLayer(&stranger);
	stack.PopOverlay(&stranger);

	CHECK(Names(stack) == Names_t{"L0", "O0"});
	CHECK(log.empty());

	stack.PushLayer(new RecordingLayer("L1", log));
	CHECK(Names(stack) == Names_t{"L0", "L1", "O0"});
}

TEST_CASE("Destroying the stack detaches then deletes in reverse stack order", "[core][layerstack]")
{
	EventLog log;
	{
		LayerStack stack;
		stack.PushLayer(new RecordingLayer("L0", log));
		stack.PushOverlay(new RecordingLayer("O0", log));
		stack.PushLayer(new RecordingLayer("L1", log));
	}

	CHECK(log == EventLog{"detach O0", "delete O0", "detach L1", "delete L1", "detach L0", "delete L0"});
}
