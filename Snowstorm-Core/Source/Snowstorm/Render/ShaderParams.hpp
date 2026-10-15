#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec4.hpp>

namespace Snowstorm
{
	enum class ShaderParamType : uint8_t
	{
		Float,
		Float2,
		Float3,
		Float4,
		Int,
		Color3,
		Color4
	};

	// One named parameter a custom surface shader packs into InstanceData::PerInstanceCustomData.
	// Declared in the shader source as
	//   // @param <Name> <float|float2|float3|float4|int|color3|color4> [slot=N] [default=a,b,..] [min=x] [max=y]
	// because the packing lives in HLSL code (Instances[i].PerInstanceCustomData.xyzw), which SPIR-V
	// reflection cannot name: the declaration is the only place the four floats get a meaning.
	// `slot` is the first component (0..3) and defaults to the first free one.
	struct ShaderParam
	{
		std::string Name;
		ShaderParamType Type = ShaderParamType::Float;
		uint8_t Slot = 0;
		uint8_t Count = 1;
		glm::vec4 Default{0.0f};
		float Min = 0.0f;
		float Max = 1.0f;

		[[nodiscard]] bool operator==(const ShaderParam&) const = default;
	};

	[[nodiscard]] uint8_t ShaderParamComponentCount(ShaderParamType type);

	// Parses every `// @param` line. Malformed lines are skipped and described in `errors` (when given), so
	// a typo costs one row, not the whole panel. Parameters that overflow the vec4 or reuse a name are rejected.
	[[nodiscard]] std::vector<ShaderParam> ParseShaderParams(std::string_view source,
	                                                         std::vector<std::string>* errors = nullptr);

	// Packs sparse named overrides over the declared defaults. Unknown names in `values` are ignored; the
	// first Count components of a value are used. Int parameters are rounded and clamped like the inspector.
	[[nodiscard]] glm::vec4 PackShaderParams(const std::vector<ShaderParam>& params,
	                                         const std::map<std::string, glm::vec4>& values);
}
