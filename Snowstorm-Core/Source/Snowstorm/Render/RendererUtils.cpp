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
