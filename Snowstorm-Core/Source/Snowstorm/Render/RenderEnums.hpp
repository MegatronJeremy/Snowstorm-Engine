#pragma once

#include <cstdint>

namespace Snowstorm
{
	enum class CompareOp : uint8_t
	{
		Never = 0,
		Less,
		Equal,
		LessOrEqual,
		Greater,
		NotEqual,
		GreaterOrEqual,
		Always
	};
}
