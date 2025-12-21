#pragma once

#include "Snowstorm/Core/Base.hpp"

#include <cstdint>
#include <string>

#include "RenderEnums.hpp"

namespace Snowstorm
{
	enum class Filter : uint8_t
	{
		Nearest = 0,
		Linear
	};

	enum class SamplerMipmapMode : uint8_t
	{
		Nearest = 0,
		Linear
	};

	enum class SamplerAddressMode : uint8_t
	{
		Repeat = 0,
		MirroredRepeat,
		ClampToEdge,
		ClampToBorder
	};

	struct SamplerDesc
	{
		Filter MinFilter = Filter::Linear;
		Filter MagFilter = Filter::Linear;
		SamplerMipmapMode MipmapMode = SamplerMipmapMode::Linear;

		SamplerAddressMode AddressU = SamplerAddressMode::Repeat;
		SamplerAddressMode AddressV = SamplerAddressMode::Repeat;
		SamplerAddressMode AddressW = SamplerAddressMode::Repeat;

		float MipLodBias = 0.0f;
		float MinLod = 0.0f;
		float MaxLod = 1000.0f;

		bool EnableAnisotropy = true;
		float MaxAnisotropy = 16.0f;

		bool EnableCompare = false; // mainly for shadow maps
		CompareOp Compare = CompareOp::LessOrEqual;

		std::string DebugName;
	};

	class Sampler
	{
	public:
		virtual ~Sampler() = default;

		[[nodiscard]] virtual const SamplerDesc& GetDesc() const = 0;

		static Ref<Sampler> Create(const SamplerDesc& desc);

	protected:
		Sampler() = default;
	};
}
