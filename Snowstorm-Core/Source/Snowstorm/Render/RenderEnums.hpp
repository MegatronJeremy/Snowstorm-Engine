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

	enum class ShaderStage : uint8_t
	{
		None     = 0,
		Vertex   = 1u << 0,
		Fragment = 1u << 1,
		Compute  = 1u << 2,
		AllGraphics = Vertex | Fragment,
		All = Vertex | Fragment | Compute
	};

	enum class PixelFormat : uint8_t
	{
		Unknown = 0,

		// Color
		RGBA8_UNorm,
		RGBA8_sRGB,
		BGRA8_UNorm, // Important for Windows Swapchains
		BGRA8_sRGB,

		// Depth
		D32_Float,
		D24_UNorm_S8_UInt,
	};

	constexpr ShaderStage operator|(ShaderStage a, ShaderStage b)
	{
		return static_cast<ShaderStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}

	constexpr bool HasStage(ShaderStage value, ShaderStage flag)
	{
		return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
	}
}
