#pragma once

#include <cstdint>
#include <string>

#include "Snowstorm/Core/Base.hpp"

namespace Snowstorm
{
	enum class TextureDimension : uint8_t
	{
		Unknown = 0,
		Texture2D,
		TextureCube,
	};

	enum class TextureFormat : uint8_t
	{
		Unknown = 0,

		// Color
		RGBA8_UNorm,
		RGBA8_sRGB,

		// Depth/stencil
		D32_Float,
		D24_UNorm_S8_UInt,
	};

	enum class TextureUsage : uint8_t
	{
		None            = 0,
		Sampled         = 1u << 0, // SRV
		Storage         = 1u << 1, // UAV
		ColorAttachment = 1u << 2, // RTV
		DepthStencil    = 1u << 3, // DSV
		TransferSrc     = 1u << 4,
		TransferDst     = 1u << 5,
	};

	constexpr TextureUsage operator|(TextureUsage a, TextureUsage b)
	{
		return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}

	constexpr TextureUsage operator&(TextureUsage a, TextureUsage b)
	{
		return static_cast<TextureUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
	}

	constexpr bool HasUsage(TextureUsage value, TextureUsage flag)
	{
		return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
	}

	enum class TextureAspect : uint8_t
	{
		Auto = 0,     // derived from format
		Color,
		Depth,
		Stencil,
		DepthStencil,
	};

	struct TextureDesc
	{
		TextureDimension Dimension = TextureDimension::Texture2D;
		TextureFormat    Format    = TextureFormat::RGBA8_sRGB;
		TextureUsage     Usage     = TextureUsage::Sampled | TextureUsage::TransferDst;

		uint32_t Width       = 1;
		uint32_t Height      = 1;
		uint32_t MipLevels   = 1;
		uint32_t ArrayLayers = 1; // Cube usually 6 (or 6*N for cube arrays)
		uint32_t SampleCount = 1;

		std::string DebugName;
	};

	struct TextureViewDesc
	{
		// If Unknown/Auto, view inherits from texture where possible
		TextureDimension Dimension = TextureDimension::Unknown;
		TextureFormat    Format    = TextureFormat::Unknown;
		TextureAspect    Aspect    = TextureAspect::Auto;

		uint32_t BaseMipLevel = 0;
		uint32_t MipLevelCount = 1;

		uint32_t BaseArrayLayer = 0;
		uint32_t ArrayLayerCount = 1;

		std::string DebugName;
	};

	class Texture
	{
	public:
		virtual ~Texture() = default;

		Texture(const Texture&) = delete;
		Texture(Texture&&) = delete;
		Texture& operator=(const Texture&) = delete;
		Texture& operator=(Texture&&) = delete;

		[[nodiscard]] virtual const TextureDesc& GetDesc() const = 0;

		[[nodiscard]] uint32_t GetWidth() const  { return GetDesc().Width; }
		[[nodiscard]] uint32_t GetHeight() const { return GetDesc().Height; }

		// Upload whole texture content (backend decides staging/queue)
		virtual void SetData(const void* data, uint32_t size) = 0;

		// Resource identity comparison (same underlying GPU resource)
		virtual bool operator==(const Texture& other) const = 0;

		static Ref<Texture> Create(const TextureDesc& desc);

	protected:
		Texture() = default;
	};

	class TextureView
	{
	public:
		virtual ~TextureView() = default;

		TextureView(const TextureView&) = delete;
		TextureView(TextureView&&) = delete;
		TextureView& operator=(const TextureView&) = delete;
		TextureView& operator=(TextureView&&) = delete;

		[[nodiscard]] virtual const TextureViewDesc& GetDesc() const = 0;
		[[nodiscard]] virtual const Ref<Texture>& GetTexture() const = 0;

		// Handle used for UI/ImGui binding 
		[[nodiscard]] virtual uint64_t GetUIID() const = 0;

		// View identity comparison (same underlying view/descriptor)
		virtual bool operator==(const TextureView& other) const = 0;

		static Ref<TextureView> Create(const Ref<Texture>& texture, const TextureViewDesc& desc);

	protected:
		TextureView() = default;
	};

	// Convenience helpers (optional, but keeps call sites tidy)
	inline TextureViewDesc MakeFullViewDesc(const TextureDesc& tex)
	{
		TextureViewDesc v{};
		v.Dimension = tex.Dimension;
		v.Format = tex.Format;
		v.Aspect = TextureAspect::Auto;
		v.BaseMipLevel = 0;
		v.MipLevelCount = tex.MipLevels;
		v.BaseArrayLayer = 0;
		v.ArrayLayerCount = tex.ArrayLayers;
		return v;
	}
}
