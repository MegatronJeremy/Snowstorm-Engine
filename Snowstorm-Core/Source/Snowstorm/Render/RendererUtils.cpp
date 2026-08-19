#include "RendererUtils.hpp"

#include "Snowstorm/Core/EngineCVars.hpp"

namespace Snowstorm
{
	Ref<RenderTarget> CreateDefaultSceneRenderTarget(uint32_t w, uint32_t h, const char* debugPrefix)
	{
		// Forward MSAA (#, apply-on-restart): when render.msaa > 1 the color+depth are multisampled and the
		// color resolves into a single-sample sampleable image at store time. samples == 1 => the original
		// single-sample path (colorTex IS the sampleable image, no resolve). See CVars::MsaaSampleCount().
		const uint32_t samples = CVars::MsaaSampleCount();

		// The single-sample, sampleable/readback color: what TAA/tonemap/upscale and dataset export read. Under
		// MSAA this is the resolve destination; without MSAA it's the render target itself.
		TextureDesc resolveDesc{};
		resolveDesc.Dimension = TextureDimension::Texture2D;
		resolveDesc.Format = kSceneColorFormat;
		// TransferSrc: this HDR scene color can be read back to the CPU for dataset export (#46). Cheap usage
		// flag; no cost unless a readback copy is actually issued.
		resolveDesc.Usage = TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::TransferSrc;
		resolveDesc.Width = w;
		resolveDesc.Height = h;
		resolveDesc.DebugName = std::string(debugPrefix) + "_Color";

		Ref<Texture> resolveTex = Texture::Create(resolveDesc);
		Ref<TextureView> resolveView = TextureView::Create(resolveTex, MakeFullViewDesc(resolveDesc));

		// The color attachment actually rendered into. samples == 1: the resolve image directly. samples > 1: a
		// dedicated multisampled image (ColorAttachment only — never sampled/transferred; it's resolved away).
		Ref<TextureView> colorView = resolveView;
		if (samples > 1)
		{
			TextureDesc msaaColorDesc{};
			msaaColorDesc.Dimension = TextureDimension::Texture2D;
			msaaColorDesc.Format = kSceneColorFormat;
			msaaColorDesc.Usage = TextureUsage::ColorAttachment;
			msaaColorDesc.SampleCount = samples;
			msaaColorDesc.Width = w;
			msaaColorDesc.Height = h;
			msaaColorDesc.DebugName = std::string(debugPrefix) + "_ColorMS";
			Ref<Texture> msaaColorTex = Texture::Create(msaaColorDesc);
			colorView = TextureView::Create(msaaColorTex, MakeFullViewDesc(msaaColorDesc));
		}

		TextureDesc depthDesc{};
		depthDesc.Dimension = TextureDimension::Texture2D;
		depthDesc.Format = PixelFormat::D32_Float;
		depthDesc.Usage = TextureUsage::DepthStencil; // depth is tested, never resolved/sampled -> plain MSAA depth
		depthDesc.SampleCount = samples;
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
		colorAtt.ResolveView = (samples > 1) ? resolveView : nullptr; // resolve MSAA -> sampleable at store
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

	Ref<RenderTarget> CreateSceneDepthPrepassTarget(const Ref<TextureView>& sceneDepthView)
	{
		// Depth-only RT wrapping the scene's OWN depth view (no color). The camera depth prepass clears +
		// writes it; the forward early-Z target below then LOADs it. Same texture -> one shared depth buffer.
		const auto& dtd = sceneDepthView->GetTexture()->GetDesc();
		RenderTargetDesc rtDesc{};
		rtDesc.Width = dtd.Width;
		rtDesc.Height = dtd.Height;
		rtDesc.IsSwapchainTarget = false;

		DepthStencilAttachment depthAtt{};
		depthAtt.View = sceneDepthView;
		depthAtt.ClearDepth = 1.0f;
		depthAtt.DepthLoadOp = RenderTargetLoadOp::Clear;
		depthAtt.DepthStoreOp = RenderTargetStoreOp::Store;
		rtDesc.DepthAttachment = depthAtt;

		return RenderTarget::Create(rtDesc);
	}

	Ref<RenderTarget> CreateForwardEarlyZTarget(const Ref<TextureView>& sceneColorView,
	                                            const Ref<TextureView>& sceneDepthView)
	{
		// Forward target for the early-Z path: the scene color view (CLEAR, matching the default scene RT) +
		// the scene depth view LOADED (the prepass already cleared + wrote it). Depth-write stays on; the
		// pipeline's LESS_EQUAL compare rejects the occluded fragments the prepass laid depth over.
		const auto& ctd = sceneColorView->GetTexture()->GetDesc();
		RenderTargetDesc rtDesc{};
		rtDesc.Width = ctd.Width;
		rtDesc.Height = ctd.Height;
		rtDesc.IsSwapchainTarget = false;

		RenderTargetAttachment colorAtt{};
		colorAtt.View = sceneColorView;
		colorAtt.AttachmentIndex = 0;
		colorAtt.ClearColor = {0.1f, 0.1f, 0.1f, 1.0f}; // match CreateDefaultSceneRenderTarget
		colorAtt.LoadOp = RenderTargetLoadOp::Clear;
		colorAtt.StoreOp = RenderTargetStoreOp::Store;
		rtDesc.ColorAttachments.push_back(colorAtt);

		DepthStencilAttachment depthAtt{};
		depthAtt.View = sceneDepthView;
		depthAtt.ClearDepth = 1.0f;
		depthAtt.DepthLoadOp = RenderTargetLoadOp::Load; // reuse the prepass depth -> early-Z
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
		// TransferSrc: motion vectors are read back to the CPU for dataset export (#46).
		colorDesc.Usage = TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::TransferSrc;
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
		// TransferSrc: the tonemapped LDR present is read back to the CPU for dataset export (#102) — the exact
		// target the neural upscaler trains against. Cheap flag; no cost unless a readback copy is issued.
		colorDesc.Usage = TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::TransferSrc;
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

	Ref<RenderTarget> CreateDepthNormalTarget(uint32_t w, uint32_t h, const char* debugPrefix)
	{
		// Partial G-buffer for half-res RT GI (#124): world-space normal color + a SAMPLED depth. The GI
		// compute pass reads both (normal directly; world position reconstructed from depth + InvViewProj),
		// and the bilateral upsample uses them as edge-stopping guides. Distinct from the scene/velocity
		// depth (DepthStencil-only, write-only) precisely because those can't be sampled.
		TextureDesc colorDesc{};
		colorDesc.Dimension = TextureDimension::Texture2D;
		// fp16 is ample for the normal (oct round-trips <0.5 deg) + roughness. Depth is NO LONGER packed here —
		// the RT consumers sample the fp32 D32 depth attachment below directly (packing NDC depth into fp16 .w
		// quantized it and banded GI/AO; NDC is non-linear). .w is now unused.
		colorDesc.Format = PixelFormat::RGBA16_SFloat; // main G-buffer: .xy oct GEOMETRIC normal, .z roughness, .w unused
		colorDesc.Usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
		colorDesc.Width = w;
		colorDesc.Height = h;
		colorDesc.DebugName = std::string(debugPrefix) + "_GBufferNormal";

		Ref<Texture> colorTex = Texture::Create(colorDesc);
		Ref<TextureView> colorView = TextureView::Create(colorTex, MakeFullViewDesc(colorDesc));

		// #129 Inc 1c: second color attachment for the NORMAL-MAPPED shading normal (.xy oct), which ONLY the
		// reflection pass reads. Split from the main G-buffer because AO/GI orient their sample hemisphere off
		// the GEOMETRIC normal (avoids self-occlusion on bumped surfaces), while reflections need the bumped
		// normal to match DefaultLit's shading. Same full-res RGBA16F shape.
		TextureDesc shadingDesc = colorDesc;
		shadingDesc.DebugName = std::string(debugPrefix) + "_GBufferShadingNormal";
		Ref<Texture> shadingTex = Texture::Create(shadingDesc);
		Ref<TextureView> shadingView = TextureView::Create(shadingTex, MakeFullViewDesc(shadingDesc));

		TextureDesc depthDesc{};
		depthDesc.Dimension = TextureDimension::Texture2D;
		depthDesc.Format = PixelFormat::D32_Float;
		// DepthStencil (written by the prepass) + Sampled (read by GI/upsample; auto-registers for bindless).
		depthDesc.Usage = TextureUsage::DepthStencil | TextureUsage::Sampled;
		depthDesc.Width = w;
		depthDesc.Height = h;
		depthDesc.DebugName = std::string(debugPrefix) + "_GBufferDepth";

		Ref<Texture> depthTex = Texture::Create(depthDesc);
		Ref<TextureView> depthView = TextureView::Create(depthTex, MakeFullViewDesc(depthDesc));

		RenderTargetDesc rtDesc{};
		rtDesc.Width = w;
		rtDesc.Height = h;
		rtDesc.IsSwapchainTarget = false;

		RenderTargetAttachment colorAtt{};
		colorAtt.View = colorView;
		colorAtt.AttachmentIndex = 0;
		colorAtt.ClearColor = {0.0f, 0.0f, 0.0f, 1.0f}; // #129 Inc 1b: sky = depth(.w)=1.0 (consumers test IsSky(depth); oct-encoded .xy has no "zero normal" sentinel anymore)
		colorAtt.LoadOp = RenderTargetLoadOp::Clear;
		colorAtt.StoreOp = RenderTargetStoreOp::Store;
		rtDesc.ColorAttachments.push_back(colorAtt);

		// #129 Inc 1c: attachment 1 = shading normal (SV_Target1 in DepthNormal.frag). Reflection pass samples
		// ColorAttachments[1].View. Cleared to 0 (sky reads a zero shading normal, but reflections gate on the
		// main G-buffer's depth, so the value there is don't-care).
		RenderTargetAttachment shadingAtt{};
		shadingAtt.View = shadingView;
		shadingAtt.AttachmentIndex = 1;
		shadingAtt.ClearColor = {0.0f, 0.0f, 0.0f, 0.0f};
		shadingAtt.LoadOp = RenderTargetLoadOp::Clear;
		shadingAtt.StoreOp = RenderTargetStoreOp::Store;
		rtDesc.ColorAttachments.push_back(shadingAtt);

		DepthStencilAttachment depthAtt{};
		depthAtt.View = depthView;
		depthAtt.ClearDepth = 1.0f;
		depthAtt.DepthLoadOp = RenderTargetLoadOp::Clear;
		depthAtt.DepthStoreOp = RenderTargetStoreOp::Store;
		rtDesc.DepthAttachment = depthAtt;

		return RenderTarget::Create(rtDesc);
	}

	Ref<Texture> CreateGITarget(uint32_t w, uint32_t h, const char* debugPrefix)
	{
		// Half-res GI irradiance (#124): compute writes it (Storage/UAV), the bilateral upsample samples it
		// (Sampled). RGBA16F to hold linear HDR bounce. A bare Texture2D, not a RenderTarget — no attachment.
		TextureDesc td{};
		td.Dimension = TextureDimension::Texture2D;
		td.Format = PixelFormat::RGBA16_SFloat;
		td.Usage = TextureUsage::Sampled | TextureUsage::Storage;
		td.Width = w;
		td.Height = h;
		td.DebugName = std::string(debugPrefix) + "_GI";
		return Texture::Create(td);
	}

	void AllocateDenoiser(DenoiserInstance& inst, const uint32_t w, const uint32_t h, const char* debugPrefix)
	{
		// All six ping-pong buffers share the CreateGITarget shape (Sampled|Storage RGBA16F UAV). One helper
		// so the two alloc systems touch a signal's denoiser in ONE place instead of ~18 repeated lines (#132).
		const std::string prefix(debugPrefix);
		for (uint32_t i = 0; i < 2; ++i)
		{
			inst.History[i] = CreateGITarget(w, h, (prefix + "_History" + std::to_string(i)).c_str());
			inst.HistoryView[i] = inst.History[i]->GetDefaultView();
			inst.Moments[i] = CreateGITarget(w, h, (prefix + "_Moments" + std::to_string(i)).c_str());
			inst.MomentsView[i] = inst.Moments[i]->GetDefaultView();
			inst.Scratch[i] = CreateGITarget(w, h, (prefix + "_Scratch" + std::to_string(i)).c_str());
			inst.ScratchView[i] = inst.Scratch[i]->GetDefaultView();
		}
		inst.Width = w;
		inst.Height = h;
		inst.HistoryValid = false; // fresh buffers: no accumulated history to reproject against
	}

	Ref<Texture> CreateAOTarget(uint32_t w, uint32_t h, const char* debugPrefix)
	{
		// Half-res AO factor (#126): compute writes it (Storage/UAV), the bilateral upsample samples it. Scalar
		// occlusion in .r; RGBA16F because the RHI has no single-channel float format (negligible at half-res).
		TextureDesc td{};
		td.Dimension = TextureDimension::Texture2D;
		td.Format = PixelFormat::RGBA16_SFloat;
		td.Usage = TextureUsage::Sampled | TextureUsage::Storage;
		td.Width = w;
		td.Height = h;
		td.DebugName = std::string(debugPrefix) + "_AO";
		return Texture::Create(td);
	}

	Ref<Texture> CreatePathTraceTarget(uint32_t w, uint32_t h, const char* debugPrefix)
	{
		// Path-tracer accumulation buffer (#153): full-res, fp32 (a converging running mean needs the precision;
		// fp16 stalls past a few hundred samples). Compute writes it (Storage/UAV); the tonemap samples it. A bare
		// Texture + view, like GITarget/AOTarget.
		TextureDesc td{};
		td.Dimension = TextureDimension::Texture2D;
		td.Format = PixelFormat::RGBA32_SFloat;
		td.Usage = TextureUsage::Sampled | TextureUsage::Storage;
		td.Width = w;
		td.Height = h;
		td.DebugName = std::string(debugPrefix) + "_PathTraceAccum";
		return Texture::Create(td);
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
