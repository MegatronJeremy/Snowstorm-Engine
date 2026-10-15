#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Timestep.hpp"
#include "Snowstorm/Events/Event.hpp"

namespace Snowstorm
{
	class Layer
	{
	public:
		explicit Layer(std::string debugName = "Layer");
		virtual ~Layer();

		Layer(const Layer&) = delete;
		Layer(Layer&&) = delete;

		Layer& operator=(const Layer&) = delete;
		Layer& operator=(Layer&&) = delete;

		virtual void OnAttach()
		{
		}

		virtual void OnDetach()
		{
		}

		virtual void OnUpdate(Timestep ts)
		{
		}

		// Smoke mode (smoke.frames) holds shutdown while this is non-empty, up to smoke.settle.seconds, and
		// fails the run if it never empties. Returns why the layer is not settled yet, or "" when it is.
		virtual std::string SmokeUnsettled() const { return {}; }

		const std::string& GetName() const { return m_DebugName; }

	protected:
		std::string m_DebugName;
	};
}
