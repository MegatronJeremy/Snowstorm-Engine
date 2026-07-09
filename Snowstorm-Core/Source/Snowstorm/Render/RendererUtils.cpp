#include "RendererUtils.hpp"

namespace Snowstorm
{
	Ref<RenderTarget> CreateDefaultSceneRenderTarget(uint32_t w, uint32_t h, const char* debugPrefix)
	{
		TextureDesc colorDesc{};
		colorDesc.Dimension = TextureDimension::Texture2D;
		colorDesc.Format = kSceneColorFormat;
		colorDesc.Usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
		colorDesc.Width = w;
		colorDesc.Height = h;
		colorDesc.DebugName = std::string(debugPrefix) + "_Color";

		Ref<Texture> colorTex = Texture::Create(colorDesc);
		Ref<TextureView> colorView = TextureView::Create(colorTex, MakeFullViewDesc(colorDesc));

		TextureDesc depthDesc{};
		depthDesc.Dimension = TextureDimension::Texture2D;
		depthDesc.Format = PixelFormat::D32_Float;
		depthDesc.Usage = TextureUsage::DepthStencil;
		depthDesc.Width = w;
		depthDesc.Height = h;
		depthDesc.DebugName = std::string(debugPrefix) + "_Depth";

		Ref<Texture> depthTex = Texture::Create(depthDesc);
		Ref<TextureView> depthView = TextureView::Create(depthTex, MakeFullViewDesc(depthDesc));

		RenderTargetDesc rtDesc{};
		rtDesc.Width = w;
		rtDesc.Height = h;
		rtDesc.IsSwapchainTarget = false;

		RenderTargetAttachment colorAtt{};
		colorAtt.View = colorView;
		colorAtt.AttachmentIndex = 0;
		colorAtt.ClearColor = {0.1f, 0.1f, 0.1f, 1.0f};
		colorAtt.LoadOp = RenderTargetLoadOp::Clear;
		colorAtt.StoreOp = RenderTargetStoreOp::Store;
		rtDesc.ColorAttachments.push_back(colorAtt);

		DepthStencilAttachment depthAtt{};
		depthAtt.View = depthView;
		depthAtt.ClearDepth = 1.0f;
		depthAtt.DepthLoadOp = RenderTargetLoadOp::Clear;
		depthAtt.DepthStoreOp = RenderTargetStoreOp::Store;
		rtDesc.DepthAttachment = depthAtt;

		return RenderTarget::Create(rtDesc);
	}

	Ref<RenderTarget> CreateVelocityTarget(uint32_t w, uint32_t h, const char* debugPrefix)
	{
		// Motion vectors (#44): RGBA16F color (.xy = velocity, cleared to 0) + its own D32 depth so the
		// velocity pass depth-tests occlusion (nearest fragment's velocity wins). Self-contained depth
		// (not the scene target's) keeps the render graph free of cross-pass depth-aliasing barriers.
		TextureDesc colorDesc{};
		colorDesc.Dimension = TextureDimension::Texture2D;
		colorDesc.Format = kVelocityFormat;
		colorDesc.Usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
		colorDesc.Width = w;
		colorDesc.Height = h;
		colorDesc.DebugName = std::string(debugPrefix) + "_Velocity";

		Ref<Texture> colorTex = Texture::Create(colorDesc);
		Ref<TextureView> colorView = TextureView::Create(colorTex, MakeFullViewDesc(colorDesc));

		TextureDesc depthDesc{};
		depthDesc.Dimension = TextureDimension::Texture2D;
		depthDesc.Format = PixelFormat::D32_Float;
		depthDesc.Usage = TextureUsage::DepthStencil;
		depthDesc.Width = w;
		depthDesc.Height = h;
		depthDesc.DebugName = std::string(debugPrefix) + "_VelocityDepth";

		Ref<Texture> depthTex = Texture::Create(depthDesc);
		Ref<TextureView> depthView = TextureView::Create(depthTex, MakeFullViewDesc(depthDesc));

		RenderTargetDesc rtDesc{};
		rtDesc.Width = w;
		rtDesc.Height = h;
		rtDesc.IsSwapchainTarget = false;

		RenderTargetAttachment colorAtt{};
		colorAtt.View = colorView;
		colorAtt.AttachmentIndex = 0;
		colorAtt.ClearColor = {0.0f, 0.0f, 0.0f, 0.0f}; // zero motion where nothing draws
		colorAtt.LoadOp = RenderTargetLoadOp::Clear;
		colorAtt.StoreOp = RenderTargetStoreOp::Store;
		rtDesc.ColorAttachments.push_back(colorAtt);

		DepthStencilAttachment depthAtt{};
		depthAtt.View = depthView;
		depthAtt.ClearDepth = 1.0f;
		depthAtt.DepthLoadOp = RenderTargetLoadOp::Clear;
		depthAtt.DepthStoreOp = RenderTargetStoreOp::Store;
		rtDesc.DepthAttachment = depthAtt;

		return RenderTarget::Create(rtDesc);
	}

	Ref<RenderTarget> CreatePresentTarget(uint32_t w, uint32_t h, const char* debugPrefix)
	{
		// LDR, color-only target for the tonemapped result. No depth (fullscreen post pass doesn't test
		// depth). sRGB storage so the hardware encodes on write; MutableFormat so ImGui can alias it with a
		// UNORM sample view (CreatePresentSampleView). Sampled usage is required for that sample view.
		TextureDesc colorDesc{};
		colorDesc.Dimension = TextureDimension::Texture2D;
		colorDesc.Format = kPresentColorFormat; // RGBA8_sRGB
		colorDesc.Usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
		colorDesc.MutableFormat = true;
		colorDesc.Width = w;
		colorDesc.Height = h;
		colorDesc.DebugName = std::string(debugPrefix) + "_Present";

		Ref<Texture> colorTex = Texture::Create(colorDesc);

		// Attachment view in the native sRGB format: rendering into it triggers the hardware linear->sRGB
		// encode. (This view also auto-registers a bindless slot as it's Sampled; unused for present, cheap.)
		Ref<TextureView> colorView = TextureView::Create(colorTex, MakeFullViewDesc(colorDesc));

		RenderTargetDesc rtDesc{};
		rtDesc.Width = w;
		rtDesc.Height = h;
		rtDesc.IsSwapchainTarget = false;

		RenderTargetAttachment colorAtt{};
		colorAtt.View = colorView;
		colorAtt.AttachmentIndex = 0;
		colorAtt.ClearColor = {0.0f, 0.0f, 0.0f, 1.0f};
		colorAtt.LoadOp = RenderTargetLoadOp::Clear;
		colorAtt.StoreOp = RenderTargetStoreOp::Store;
		rtDesc.ColorAttachments.push_back(colorAtt);
		// No depth attachment.

		return RenderTarget::Create(rtDesc);
	}

	Ref<RenderTarget> CreateColorOnlyHDRTarget(uint32_t w, uint32_t h, const char* debugPrefix)
	{
		// HDR color, NO depth — for a fullscreen HDR post pass (e.g. UpscalePass) whose pipeline declares no
		// depth format. A depth attachment here would mismatch the pipeline's (undefined) depth format under
		// dynamic rendering. Sampled so tonemap can bindless-Load the result.
		TextureDesc colorDesc{};
		colorDesc.Dimension = TextureDimension::Texture2D;
		colorDesc.Format = kSceneColorFormat; // RGBA16F, matches the scene target so tonemap's Load matches
		colorDesc.Usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
		colorDesc.Width = w;
		colorDesc.Height = h;
		colorDesc.DebugName = std::string(debugPrefix) + "_Color";

		Ref<Texture> colorTex = Texture::Create(colorDesc);
		Ref<TextureView> colorView = TextureView::Create(colorTex, MakeFullViewDesc(colorDesc));

		RenderTargetDesc rtDesc{};
		rtDesc.Width = w;
		rtDesc.Height = h;
		rtDesc.IsSwapchainTarget = false;

		RenderTargetAttachment colorAtt{};
		colorAtt.View = colorView;
		colorAtt.AttachmentIndex = 0;
		colorAtt.ClearColor = {0.0f, 0.0f, 0.0f, 1.0f};
		colorAtt.LoadOp = RenderTargetLoadOp::Clear;
		colorAtt.StoreOp = RenderTargetStoreOp::Store;
		rtDesc.ColorAttachments.push_back(colorAtt);
		// No depth attachment.

		return RenderTarget::Create(rtDesc);
	}

	Ref<TextureView> CreatePresentSampleView(const Ref<RenderTarget>& presentTarget)
	{
		if (!presentTarget)
		{
			return nullptr;
		}
		const auto& desc = presentTarget->GetDesc();
		if (desc.ColorAttachments.empty() || !desc.ColorAttachments[0].View)
		{
			return nullptr;
		}

		// Alias the sRGB present image with a UNORM view so ImGui samples the encoded bytes verbatim (no
		// hardware sRGB decode). Same subresource range as the attachment view, only the format differs.
		const Ref<Texture>& img = desc.ColorAttachments[0].View->GetTexture();
		TextureViewDesc v = MakeFullViewDesc(img->GetDesc());
		v.Format = kPresentSampleFormat; // UNORM twin
		v.DebugName = "PresentSample_UNORM";
		return TextureView::Create(img, v);
	}

	Ref<RenderTarget> CreateShadowDepthTarget(const uint32_t size, const char* debugPrefix)
	{
		TextureDesc depthDesc{};
		depthDesc.Dimension = TextureDimension::Texture2D;
		depthDesc.Format = PixelFormat::D32_Float;
		// DepthStencil: written as a depth attachment by the shadow pass. Sampled: read back in the lit
		// shader (also auto-registers the view for bindless sampling).
		depthDesc.Usage = TextureUsage::DepthStencil | TextureUsage::Sampled;
		depthDesc.Width = size;
		depthDesc.Height = size;
		depthDesc.DebugName = std::string(debugPrefix) + "_ShadowDepth";

		Ref<Texture> depthTex = Texture::Create(depthDesc);
		Ref<TextureView> depthView = TextureView::Create(depthTex, MakeFullViewDesc(depthDesc));

		RenderTargetDesc rtDesc{};
		rtDesc.Width = size;
		rtDesc.Height = size;
		rtDesc.IsSwapchainTarget = false;
		// No color attachment — depth-only pass.

		DepthStencilAttachment depthAtt{};
		depthAtt.View = depthView;
		depthAtt.ClearDepth = 1.0f;
		depthAtt.DepthLoadOp = RenderTargetLoadOp::Clear;
		depthAtt.DepthStoreOp = RenderTargetStoreOp::Store;
		rtDesc.DepthAttachment = depthAtt;

		return RenderTarget::Create(rtDesc);
	}

	Ref<Texture> CreateCubeTexture(const uint32_t size, const uint32_t mips, const PixelFormat format, const char* debugName)
	{
		TextureDesc td{};
		td.Dimension = TextureDimension::TextureCube;
		td.Format = format;
		// Sampled (read in the lit shader), Storage (compute writes faces/mips), ColorAttachment (the env
		// capture may render into faces), TransferSrc/Dst (mip blits if needed).
		td.Usage = TextureUsage::Sampled | TextureUsage::Storage | TextureUsage::ColorAttachment |
		           TextureUsage::TransferSrc | TextureUsage::TransferDst;
		td.Width = size;
		td.Height = size;
		td.MipLevels = mips;
		td.ArrayLayers = 6;
		td.DebugName = debugName;
		return Texture::Create(td);
	}

	Ref<TextureView> MakeFaceMipView(const Ref<Texture>& cube, const uint32_t face, const uint32_t mip)
	{
		const TextureDesc& cd = cube->GetDesc();
		TextureViewDesc v{};
		v.Dimension = TextureDimension::Texture2D; // single face+mip is a 2D view
		v.Format = cd.Format;
		v.Aspect = TextureAspect::Auto;
		v.BaseMipLevel = mip;
		v.MipLevelCount = 1;
		v.BaseArrayLayer = face;
		v.ArrayLayerCount = 1;
		v.DebugName = std::string(cd.DebugName) + "_face" + std::to_string(face) + "_mip" + std::to_string(mip);
		return TextureView::Create(cube, v);
	}
}
