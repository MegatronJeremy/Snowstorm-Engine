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

		const std::string& GetName() const { return m_DebugName; }

	protected:
		std::string m_DebugName;
	};
}
