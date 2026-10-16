#pragma once

#include "Snowstorm/Render/DatasetExport/NpyWriter.hpp"
#include "Snowstorm/Render/RenderEnums.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace Snowstorm::QualityCaptureFormat
{
	// The HDR capture is the raw texel format of SceneColor, written unconverted: RGBA16F as '<f2' and the
	// path tracer's RGBA32F accumulation as '<f4'. Any other format has no HDR capture.
	inline std::optional<NpyDType> HdrDType(const PixelFormat f)
	{
		switch (f)
		{
		case PixelFormat::RGBA16_SFloat:
			return NpyDType::Float16;
		case PixelFormat::RGBA32_SFloat:
			return NpyDType::Float32;
		default:
			return std::nullopt;
		}
	}

	inline uint32_t HdrBytesPerPixel(const PixelFormat f)
	{
		switch (f)
		{
		case PixelFormat::RGBA16_SFloat:
			return 8;
		case PixelFormat::RGBA32_SFloat:
			return 16;
		default:
			return 0;
		}
	}

	// <basePath>_hdr.npy, or <basePath>_f<NNNNNN>_hdr.npy for a motion-sequence frame (`frameSuffix` is the
	// zero-padded route frame): the sibling of the matching _ldr.npy.
	inline std::string HdrPath(const std::string& basePath, const std::string& frameSuffix = {})
	{
		return basePath + (frameSuffix.empty() ? "" : "_f" + frameSuffix) + "_hdr.npy";
	}
}
