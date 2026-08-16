#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Texture.hpp"
#include "Snowstorm/Math/Math.hpp"

#include <optional>
#include <vector>

namespace Snowstorm
{
	enum class RenderTargetLoadOp : uint8_t
	{
		Load,
		Clear,
		DontCare
	};

	enum class RenderTargetStoreOp : uint8_t
	{
		Store,
		DontCare
	};

	struct RenderTargetAttachment
	{
		// View to render into (RTV-like). Under MSAA this is the multisampled image.
		Ref<TextureView> View;

		// Optional MSAA resolve destination (single-sample). When set, the pass averages `View`'s
		// samples into this image at store time (dynamic-rendering resolveImageView). Downstream reads
		// the resolved single-sample image, not `View`. Null = no resolve (single-sample rendering).
		Ref<TextureView> ResolveView;

		// Optional: attachment index for APIs that care (MRT)
		uint32_t AttachmentIndex = 0;

		// Clear color used when LoadOp == Clear
		glm::vec4 ClearColor{0.0f, 0.0f, 0.0f, 1.0f};

		RenderTargetLoadOp LoadOp = RenderTargetLoadOp::Clear;
		RenderTargetStoreOp StoreOp = RenderTargetStoreOp::Store;
	};

	struct DepthStencilAttachment
	{
		// View to render into (DSV-like)
		Ref<TextureView> View;

		float ClearDepth = 1.0f;
		uint32_t ClearStencil = 0;

		RenderTargetLoadOp DepthLoadOp = RenderTargetLoadOp::Clear;
		RenderTargetStoreOp DepthStoreOp = RenderTargetStoreOp::Store;
		RenderTargetLoadOp StencilLoadOp = RenderTargetLoadOp::DontCare;
		RenderTargetStoreOp StencilStoreOp = RenderTargetStoreOp::DontCare;
	};

	struct RenderTargetDesc
	{
		uint32_t Width = 0;
		uint32_t Height = 0;

		std::vector<RenderTargetAttachment> ColorAttachments;
		std::optional<DepthStencilAttachment> DepthAttachment;

		// Whether this represents the "default" backbuffer (swapchain image)
		bool IsSwapchainTarget = false;
	};

	class RenderTarget
	{
	public:
		virtual ~RenderTarget() = default;

		virtual const RenderTargetDesc& GetDesc() const = 0;
		virtual void Resize(uint32_t width, uint32_t height) = 0;

		// The single-sample, sampleable view of color attachment `i`: the resolve image when this is an MSAA
		// target, else the attachment itself. Downstream passes (TAA/tonemap/upscale) sample THIS, never the
		// multisampled attachment. Null if the index is out of range.
		Ref<TextureView> GetSampleableColorView(size_t i = 0) const
		{
			const auto& atts = GetDesc().ColorAttachments;
			if (i >= atts.size())
			{
				return nullptr;
			}
			return atts[i].ResolveView ? atts[i].ResolveView : atts[i].View;
		}

		uint32_t GetWidth() const { return GetDesc().Width; }
		uint32_t GetHeight() const { return GetDesc().Height; }

		bool IsSwapchainTarget() const { return GetDesc().IsSwapchainTarget; }

		static Ref<RenderTarget> Create(const RenderTargetDesc& desc);
	};
}
