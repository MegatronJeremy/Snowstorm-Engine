#include "pch.h"
#include "Layer.hpp"

namespace Snowstorm
{
	Layer::Layer(std::string debugName)
		: m_DebugName(std::move(debugName))
	{
	}

	Layer::~Layer()
	= default;
}
