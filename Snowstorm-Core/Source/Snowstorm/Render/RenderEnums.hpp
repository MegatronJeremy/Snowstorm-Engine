#pragma once

#include <cstdint>

namespace Snowstorm
{
	enum class DepthFunction : uint8_t
	{
		Never,
		Less,
		Equal,
		LessEqual,
		Greater,
		NotEqual,
		GreaterEqual,
		Always
	};
}
