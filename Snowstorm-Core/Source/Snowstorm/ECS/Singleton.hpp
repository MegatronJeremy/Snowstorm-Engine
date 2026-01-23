#pragma once

#include "Snowstorm/Utility/NonCopyable.hpp"

namespace Snowstorm
{
	class Singleton : public NonCopyable
	{
	public:
		Singleton() = default;
	};
}
