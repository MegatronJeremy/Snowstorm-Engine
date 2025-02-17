#pragma once
#include "Snowstorm/Core/Timestep.hpp"
#include "Snowstorm/Utility/NonCopyable.hpp"

namespace Snowstorm
{
	class Service : public NonCopyable
	{
	public:
		virtual void OnUpdate(Timestep ts) = 0;
		virtual void PostUpdate(Timestep ts) = 0;
	};
}
