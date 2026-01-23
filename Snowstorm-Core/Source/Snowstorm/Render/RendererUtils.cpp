#include "RendererUtils.hpp"

namespace Snowstorm
{
	Ref<RenderTarget> CreateDefaultSceneRenderTarget(uint32_t w, uint32_t h, const char* debugPrefix)
	{
		TextureDesc colorDesc{};
		colorDesc.Dimension = TextureDimension::Texture2D;
		colorDesc.Format = PixelFormat::RGBA8_UNorm;
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
}
