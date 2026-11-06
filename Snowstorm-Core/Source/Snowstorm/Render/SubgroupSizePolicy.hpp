#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Snowstorm
{
	// Which subgroup (wave) size a compute pipeline asks the driver for. 0 means "let the driver choose".

	// Overrides are "Name=Size,Name=Size"; Name is matched as a substring of the shader path (case-sensitive,
	// first entry that matches wins). Malformed entries and sizes that are not a power of two are skipped.
	[[nodiscard]] inline uint32_t SubgroupOverrideFor(const std::string_view overrides, const std::string_view shaderPath)
	{
		size_t pos = 0;
		while (pos < overrides.size())
		{
			size_t end = overrides.find(',', pos);
			if (end == std::string_view::npos)
			{
				end = overrides.size();
			}
			const std::string_view entry = overrides.substr(pos, end - pos);
			pos = end + 1;

			const size_t eq = entry.find('=');
			if (eq == std::string_view::npos || eq == 0)
			{
				continue;
			}
			const std::string_view name = entry.substr(0, eq);
			const std::string_view num = entry.substr(eq + 1);
			if (num.empty() || num.size() > 4 || shaderPath.find(name) == std::string_view::npos)
			{
				continue;
			}
			uint32_t size = 0;
			bool digits = true;
			for (const char c : num)
			{
				if (c < '0' || c > '9')
				{
					digits = false;
					break;
				}
				size = size * 10 + static_cast<uint32_t>(c - '0');
			}
			if (digits && size != 0 && (size & (size - 1)) == 0)
			{
				return size;
			}
		}
		return 0;
	}

	// The size to put in VkPipelineShaderStageRequiredSubgroupSizeCreateInfo, or 0 for no request. A per-shader
	// override beats the global size. A size outside the device's [minSize, maxSize] is dropped (0), since the
	// spec makes requiring an unsupported size invalid usage; `stageSupported` is the device's
	// requiredSubgroupSizeStages bit for compute.
	[[nodiscard]] inline uint32_t ResolveRequiredSubgroupSize(const int32_t globalSize, const std::string_view overrides,
	                                                          const std::string_view shaderPath, const uint32_t minSize,
	                                                          const uint32_t maxSize, const bool stageSupported)
	{
		if (!stageSupported)
		{
			return 0;
		}
		uint32_t want = SubgroupOverrideFor(overrides, shaderPath);
		if (want == 0 && globalSize > 0)
		{
			want = static_cast<uint32_t>(globalSize);
		}
		if (want == 0 || want < minSize || want > maxSize || (want & (want - 1)) != 0)
		{
			return 0;
		}
		return want;
	}
} // namespace Snowstorm
