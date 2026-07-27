#include "IBLBakePass.hpp"

#include "Snowstorm/Assets/IBLCache.hpp"
#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/RendererUtils.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstring>

namespace Snowstorm
{
	namespace
	{
		// IBL bake resolutions (shared by the bake + the prefiltered mip count fed to FrameCB).
		constexpr uint32_t kEnvCubeSize = 128;
		constexpr uint32_t kIrradianceCubeSize = 32;
		constexpr uint32_t kPrefilterCubeSize = 128;
		constexpr uint32_t kPrefilterMips = 5; // roughness 0..1 across mips
		constexpr uint32_t kBRDFLutSize = 256;

		// Mirrors IBLCapture.hlsl IBLParams (16-byte-aligned rows). FaceIndex selects the cube face.
		struct CaptureParams
		{
			glm::vec3 SkyZenithColor;
			float _p0 = 0.0f;
			glm::vec3 SkyHorizonColor;
			float _p1 = 0.0f;
			glm::vec3 GroundColor;
			float _p2 = 0.0f;
			glm::vec3 ToSun;
			float _p3 = 0.0f;
			glm::vec3 SunColor;
			uint32_t FaceIndex = 0;
		};

		// Mirrors IBLIrradiance.hlsl IrradianceParams.
		struct IrradianceParams
		{
			uint32_t FaceIndex = 0;
			float _p0 = 0.0f;
			float _p1 = 0.0f;
			float _p2 = 0.0f;
		};

		// Mirrors IBLPrefilter.hlsl PrefilterParams.
		struct PrefilterParams
		{
			float Roughness = 0.0f;
			uint32_t FaceIndex = 0;
			float _p0 = 0.0f;
			float _p1 = 0.0f;
		};
	}

	uint32_t IBLBakePass::IrradianceIndex() const
	{
		return m_IrradianceCubeView ? m_IrradianceCubeView->GetGlobalBindlessIndex() : 0u;
	}

	uint32_t IBLBakePass::PrefilteredIndex() const
	{
		return m_PrefilteredCubeView ? m_PrefilteredCubeView->GetGlobalBindlessIndex() : 0u;
	}

	uint32_t IBLBakePass::BRDFLutIndex() const
	{
		return m_BRDFLutView ? m_BRDFLutView->GetGlobalBindlessIndex() : 0u;
	}

	uint32_t IBLBakePass::PrefilteredMipCount() const
	{
		return kPrefilterMips;
	}

	void IBLBakePass::EnsureOutputTextures()
	{
		if (m_IrradianceCube)
		{
			return; // already created (either path)
		}

		// The three output maps + the LUT. TransferDst is needed so the cache-hit path can upload into them
		// (SetCubeData / SetData); Storage is needed so the miss-path compute can write them.
		m_IrradianceCube = CreateCubeTexture(kIrradianceCubeSize, 1, PixelFormat::RGBA16_SFloat, "IBL_IrradianceCube");
		m_PrefilteredCube = CreateCubeTexture(kPrefilterCubeSize, kPrefilterMips, PixelFormat::RGBA16_SFloat, "IBL_PrefilteredCube");

		// 2D BRDF LUT (RG16F is enough, but reuse RGBA16F to avoid another format dependency). Storage (compute
		// writes it), Sampled (shading reads it), TransferDst (cache-hit upload), TransferSrc (cache-save readback).
		TextureDesc lutDesc{};
		lutDesc.Dimension = TextureDimension::Texture2D;
		lutDesc.Format = PixelFormat::RGBA16_SFloat;
		lutDesc.Usage = TextureUsage::Sampled | TextureUsage::Storage | TextureUsage::TransferDst | TextureUsage::TransferSrc;
		lutDesc.Width = kBRDFLutSize;
		lutDesc.Height = kBRDFLutSize;
		lutDesc.DebugName = "IBL_BRDFLut";
		m_BRDFLut = Texture::Create(lutDesc);

		// Full-resource sampled views, kept alive: these auto-register in the bindless arrays (indices feed FrameCB).
		m_IrradianceCubeView = m_IrradianceCube->GetDefaultView();
		m_PrefilteredCubeView = m_PrefilteredCube->GetDefaultView();
		m_BRDFLutView = m_BRDFLut->GetDefaultView();
	}

	bool IBLBakePass::EnsureBakePipelines()
	{
		if (m_CapturePipeline)
		{
			return true;
		}

		// Load via the app-scoped ShaderLibrary (not Shader::Create) so these compute shaders register for
		// hot-reload; the reload sweep then rebuilds the bake pipelines when a source changes.
		auto& shaderLib = Application::Get().GetServiceManager().GetService<ShaderLibrary>();
		Ref<Shader> captureCs = shaderLib.Load("Engine/Shaders/IBLCapture.hlsl");
		Ref<Shader> irradianceCs = shaderLib.Load("Engine/Shaders/IBLIrradiance.hlsl");
		Ref<Shader> prefilterCs = shaderLib.Load("Engine/Shaders/IBLPrefilter.hlsl");
		Ref<Shader> brdfCs = shaderLib.Load("Engine/Shaders/IBLBRDFLut.hlsl");
		if (!captureCs || !irradianceCs || !prefilterCs || !brdfCs)
		{
			SS_CORE_ERROR("[IBL] failed to load bake compute shaders");
			return false;
		}

		// Compute shaders compile asynchronously (ShaderLibrary::Load submits to a worker). Building a
		// compute pipeline from a not-ready shader would assert on empty SPIR-V, so bail until all four are
		// ready — m_CapturePipeline stays null and AddBakePasses retries next frame (the bake is already
		// re-driven per frame and gated on a real environment in RenderSystem). This is the compute-side
		// mirror of GetOrCreatePipeline's readiness gate.
		if (!captureCs->IsReady() || !irradianceCs->IsReady() || !prefilterCs->IsReady() || !brdfCs->IsReady())
		{
			return false;
		}

		const auto makeComputePipe = [](const Ref<Shader>& cs, const char* name)
		{
			PipelineDesc p{};
			p.Type = PipelineType::Compute;
			p.Shader = cs;
			p.DebugName = name;
			return Pipeline::Create(p);
		};
		m_CapturePipeline = makeComputePipe(captureCs, "IBLCapturePipeline");
		m_IrradiancePipeline = makeComputePipe(irradianceCs, "IBLIrradiancePipeline");
		m_PrefilterPipeline = makeComputePipe(prefilterCs, "IBLPrefilterPipeline");
		m_BRDFLutPipeline = makeComputePipe(brdfCs, "IBLBRDFLutPipeline");

		// Intermediate env cube (miss-path only): the sky capture convolved by irradiance + prefilter. Not a
		// bindless output, so no TransferDst.
		m_EnvCube = CreateCubeTexture(kEnvCubeSize, 1, PixelFormat::RGBA16_SFloat, "IBL_EnvCube");
		m_EnvCubeView = m_EnvCube->GetDefaultView(); // bound as the convolution source

		SamplerDesc sd{};
		sd.MinFilter = Filter::Linear;
		sd.MagFilter = Filter::Linear;
		sd.AddressU = SamplerAddressMode::ClampToEdge;
		sd.AddressV = SamplerAddressMode::ClampToEdge;
		sd.AddressW = SamplerAddressMode::ClampToEdge;
		sd.EnableAnisotropy = false;
		sd.DebugName = "IBLSampler";
		m_Sampler = Sampler::Create(sd);
		return true;
	}

	void IBLBakePass::AddBakePasses(RenderGraph& graph, const LightDataBlock& lights, const EnvironmentDataBlock& environment)
	{
		if (m_Baked)
		{
			return;
		}

		// A re-bake (environment change, #64) would clobber a still-armed save from the PREVIOUS bake before
		// PumpCacheSave got to it. RenderSystem drained the GPU (Renderer::WaitIdle) before this call, so the
		// prior readback definitely completed — flush that save now, synchronously, so no bake is lost.
		if (m_CacheSaveCountdown > 0)
		{
			m_CacheSaveCountdown = 1; // fire on this tick
			PumpCacheSave();
		}

		// ---- Disk-cache hit (#34): the maps for this exact sky+sun already exist on disk. Upload them and
		// skip the whole GPU bake (compute + the 4 async shader compiles) — the ~300 ms cold-start saving. ----
		const uint64_t envHash = HashIBLEnvironment(environment, lights);
		if (const std::optional<CookedIBL> cached =
		        IBLCacheIO::Load(envHash, kIrradianceCubeSize, kPrefilterCubeSize, kPrefilterMips, kBRDFLutSize))
		{
			EnsureOutputTextures();
			UploadFromCache(*cached);
			SS_CORE_INFO("[IBL] loaded maps from cache (hash={:016x}) -- skipped bake", envHash);
			return;
		}

		// ---- Miss: bake on GPU as before, then read the maps back and save them for next time. ----
		EnsureOutputTextures();
		if (!EnsureBakePipelines())
		{
			return; // shaders still compiling (or load failed) — retry next frame
		}

		// Environment params from the current sky (linear HDR; sun = DirectionalLights[0]).
		const bool haveSun = lights.LightCount > 0;
		const glm::vec3 toSun = haveSun ? glm::normalize(-lights.Lights[0].Direction) : glm::vec3(0.0f);
		const glm::vec3 sunColor = haveSun ? lights.Lights[0].Color * lights.Lights[0].Intensity : glm::vec3(0.0f);

		// ---- Pass 1: capture the sky into the env cube (6 faces) ----
		// Writes envCube as Storage; the graph transitions it to GENERAL before the dispatches.
		graph.AddPass({.Name = "IBLCapture",
		               .IsCompute = true,
		               .Writes = {{m_EnvCube, RenderGraph::AccessState::Storage}},
		               .Execute = [this, environment, toSun, sunColor](CommandContext& ctx)
		               {
			               ctx.BindPipeline(m_CapturePipeline);
			               const auto& layouts = m_CapturePipeline->GetSetLayouts();
			               SS_CORE_ASSERT(!layouts.empty(), "[IBL] capture pipeline has no set layout");
			               for (uint32_t face = 0; face < 6; ++face)
			               {
				               CaptureParams p{};
				               p.SkyZenithColor = environment.SkyZenithColor;
				               p.SkyHorizonColor = environment.SkyHorizonColor;
				               p.GroundColor = environment.GroundColor;
				               p.ToSun = toSun;
				               p.SunColor = sunColor;
				               p.FaceIndex = face;

				               Ref<Buffer> ubo = Buffer::Create(sizeof(CaptureParams), BufferUsage::Uniform, &p, true, "IBLCaptureParams");
				               Ref<TextureView> faceView = MakeFaceMipView(m_EnvCube, face, 0);

				               DescriptorSetDesc dsd{};
				               dsd.DebugName = "IBLCaptureSet";
				               Ref<DescriptorSet> set = DescriptorSet::Create(layouts[0], dsd);
				               BufferBinding bb{.Buffer = ubo, .Offset = 0, .Range = sizeof(CaptureParams)};
				               set->SetBuffer(0, bb);
				               set->SetTexture(1, faceView); // u1 storage face
				               set->Commit();

				               ctx.BindDescriptorSet(set, 0);
				               ctx.Dispatch((kEnvCubeSize + 7) / 8, (kEnvCubeSize + 7) / 8, 1);

				               m_BakeKeepAlive.push_back(ubo);
				               m_BakeKeepAlive.push_back(set);
				               m_BakeKeepAlive.push_back(faceView);
			               }
		               }});

		// ---- Pass 2: cosine-convolve env -> irradiance cube (6 faces) ----
		// Reads envCube (Sampled) — the graph's Storage->Sampled transition is the write-before-read barrier.
		graph.AddPass({.Name = "IBLIrradiance",
		               .IsCompute = true,
		               .Reads = {{m_EnvCube, RenderGraph::AccessState::Sampled}},
		               .Writes = {{m_IrradianceCube, RenderGraph::AccessState::Storage}},
		               .Execute = [this](CommandContext& ctx)
		               {
			               ctx.BindPipeline(m_IrradiancePipeline);
			               const auto& layouts = m_IrradiancePipeline->GetSetLayouts();
			               SS_CORE_ASSERT(!layouts.empty(), "[IBL] irradiance pipeline has no set layout");
			               for (uint32_t face = 0; face < 6; ++face)
			               {
				               IrradianceParams p{};
				               p.FaceIndex = face;

				               Ref<Buffer> ubo = Buffer::Create(sizeof(IrradianceParams), BufferUsage::Uniform, &p, true, "IBLIrradianceParams");
				               Ref<TextureView> faceView = MakeFaceMipView(m_IrradianceCube, face, 0);

				               DescriptorSetDesc dsd{};
				               dsd.DebugName = "IBLIrradianceSet";
				               Ref<DescriptorSet> set = DescriptorSet::Create(layouts[0], dsd);
				               BufferBinding bb{.Buffer = ubo, .Offset = 0, .Range = sizeof(IrradianceParams)};
				               set->SetBuffer(0, bb);
				               set->SetTexture(1, m_EnvCubeView); // t1 sampled env cube (kept-alive member)
				               set->SetSampler(2, m_Sampler);     // s2
				               set->SetTexture(3, faceView);      // u3 storage face
				               set->Commit();

				               ctx.BindDescriptorSet(set, 0);
				               ctx.Dispatch((kIrradianceCubeSize + 7) / 8, (kIrradianceCubeSize + 7) / 8, 1);

				               m_BakeKeepAlive.push_back(ubo);
				               m_BakeKeepAlive.push_back(set);
				               m_BakeKeepAlive.push_back(faceView);
			               }
		               }});

		// ---- Pass 3: GGX prefilter env -> prefiltered cube, one dispatch per (mip, face) ----
		graph.AddPass({.Name = "IBLPrefilter",
		               .IsCompute = true,
		               .Reads = {{m_EnvCube, RenderGraph::AccessState::Sampled}},
		               .Writes = {{m_PrefilteredCube, RenderGraph::AccessState::Storage}},
		               .Execute = [this](CommandContext& ctx)
		               {
			               ctx.BindPipeline(m_PrefilterPipeline);
			               const auto& layouts = m_PrefilterPipeline->GetSetLayouts();
			               SS_CORE_ASSERT(!layouts.empty(), "[IBL] prefilter pipeline has no set layout");
			               for (uint32_t mip = 0; mip < kPrefilterMips; ++mip)
			               {
				               const uint32_t mipSize = kPrefilterCubeSize >> mip;
				               const float roughness = (kPrefilterMips > 1) ? static_cast<float>(mip) / static_cast<float>(kPrefilterMips - 1) : 0.0f;
				               for (uint32_t face = 0; face < 6; ++face)
				               {
					               PrefilterParams p{};
					               p.Roughness = roughness;
					               p.FaceIndex = face;

					               Ref<Buffer> ubo = Buffer::Create(sizeof(PrefilterParams), BufferUsage::Uniform, &p, true, "IBLPrefilterParams");
					               Ref<TextureView> faceView = MakeFaceMipView(m_PrefilteredCube, face, mip);

					               DescriptorSetDesc dsd{};
					               dsd.DebugName = "IBLPrefilterSet";
					               Ref<DescriptorSet> set = DescriptorSet::Create(layouts[0], dsd);
					               BufferBinding bb{.Buffer = ubo, .Offset = 0, .Range = sizeof(PrefilterParams)};
					               set->SetBuffer(0, bb);
					               set->SetTexture(1, m_EnvCubeView); // t1 sampled env cube
					               set->SetSampler(2, m_Sampler);     // s2
					               set->SetTexture(3, faceView);      // u3 storage face+mip
					               set->Commit();

					               ctx.BindDescriptorSet(set, 0);
					               ctx.Dispatch((mipSize + 7) / 8, (mipSize + 7) / 8, 1);

					               m_BakeKeepAlive.push_back(ubo);
					               m_BakeKeepAlive.push_back(set);
					               m_BakeKeepAlive.push_back(faceView);
				               }
			               }
		               }});

		// ---- Pass 4: BRDF integration LUT (2D, environment-independent) ----
		graph.AddPass({.Name = "IBLBRDFLut",
		               .IsCompute = true,
		               .Writes = {{m_BRDFLut, RenderGraph::AccessState::Storage}},
		               .Execute = [this](CommandContext& ctx)
		               {
			               ctx.BindPipeline(m_BRDFLutPipeline);
			               const auto& layouts = m_BRDFLutPipeline->GetSetLayouts();
			               SS_CORE_ASSERT(!layouts.empty(), "[IBL] BRDF LUT pipeline has no set layout");
			               DescriptorSetDesc dsd{};
			               dsd.DebugName = "IBLBRDFLutSet";
			               Ref<DescriptorSet> set = DescriptorSet::Create(layouts[0], dsd);
			               set->SetTexture(0, m_BRDFLutView); // u0 storage 2D LUT
			               set->Commit();

			               ctx.BindDescriptorSet(set, 0);
			               ctx.Dispatch((kBRDFLutSize + 7) / 8, (kBRDFLutSize + 7) / 8, 1);
			               m_BakeKeepAlive.push_back(set);
		               }});

		m_Baked = true;
		SS_CORE_INFO("[IBL] baked all maps -- irradiance={} prefiltered={} (cube bindless) brdfLut={} (2d bindless)",
		             m_IrradianceCubeView->GetGlobalBindlessIndex(),
		             m_PrefilteredCubeView->GetGlobalBindlessIndex(),
		             m_BRDFLutView->GetGlobalBindlessIndex());

		// Read the freshly-baked maps back so PumpCacheSave() can write the .ssibl next frame. The readback
		// pass runs after the bake passes above (same frame), reading each map Sampled. Cold path only — a
		// cache hit returned early and never reaches here.
		m_PendingEnvHash = envHash;
		AddReadbackPass(graph);
	}

	void IBLBakePass::UploadFromCache(const CookedIBL& ibl)
	{
		m_IrradianceCube->SetCubeData(ibl.Irradiance);
		m_PrefilteredCube->SetCubeData(ibl.Prefiltered);
		m_BRDFLut->SetData(ibl.BRDFLut.data(), static_cast<uint32_t>(ibl.BRDFLut.size()));
		m_Baked = true;
	}

	void IBLBakePass::AddReadbackPass(RenderGraph& graph)
	{
		constexpr uint32_t kTexelBytes = 8; // RGBA16_SFloat

		// One host-visible buffer per subresource, ordered irradiance faces (6) -> prefiltered face-major
		// (6 * mips) -> LUT (1). PumpCacheSave maps them in this same order.
		m_ReadbackBuffers.clear();
		const auto addBuf = [&](const uint32_t w, const uint32_t h, const char* dbg)
		{
			m_ReadbackBuffers.push_back(Buffer::Create(static_cast<size_t>(w) * h * kTexelBytes,
			                                           BufferUsage::Readback, nullptr, true, dbg));
		};
		for (uint32_t f = 0; f < 6; ++f)
		{
			addBuf(kIrradianceCubeSize, kIrradianceCubeSize, "IBLReadback_Irr");
		}
		for (uint32_t f = 0; f < 6; ++f)
		{
			for (uint32_t m = 0; m < kPrefilterMips; ++m)
			{
				const uint32_t s = std::max(1u, kPrefilterCubeSize >> m);
				addBuf(s, s, "IBLReadback_Pref");
			}
		}
		addBuf(kBRDFLutSize, kBRDFLutSize, "IBLReadback_Lut");

		graph.AddPass({.Name = "IBLReadback",
		               .IsCompute = true, // no render target; records copies
		               .Reads = {{m_IrradianceCube, RenderGraph::AccessState::Sampled},
		                         {m_PrefilteredCube, RenderGraph::AccessState::Sampled},
		                         {m_BRDFLut, RenderGraph::AccessState::Sampled}},
		               .Execute = [this](CommandContext& ctx)
		               {
			               uint32_t b = 0;
			               for (uint32_t f = 0; f < 6; ++f)
			               {
				               ctx.CopyTextureToBuffer(m_IrradianceCube, m_ReadbackBuffers[b++], 0, f);
			               }
			               for (uint32_t f = 0; f < 6; ++f)
			               {
				               for (uint32_t m = 0; m < kPrefilterMips; ++m)
				               {
					               ctx.CopyTextureToBuffer(m_PrefilteredCube, m_ReadbackBuffers[b++], m, f);
				               }
			               }
			               ctx.CopyTextureToBuffer(m_BRDFLut, m_ReadbackBuffers[b++], 0, 0);
		               }});

		// Arm the deferred save. This readback pass executes at the END of the current frame; wait 2
		// PumpCacheSave ticks so the copy's fence has retired before we Map() the buffers (mapping the same
		// frame would read before the GPU wrote — the crash this replaced).
		m_CacheSaveCountdown = 2;
	}

	void IBLBakePass::PumpCacheSave()
	{
		if (m_CacheSaveCountdown <= 0)
		{
			return;
		}
		if (--m_CacheSaveCountdown > 0)
		{
			return; // still waiting for the readback submit's fence to retire
		}

		constexpr uint32_t kTexelBytes = 8; // RGBA16_SFloat
		const auto readBuf = [](const Ref<Buffer>& buf, const uint32_t w, const uint32_t h)
		{
			std::vector<uint8_t> out(static_cast<size_t>(w) * h * kTexelBytes);
			if (const void* p = buf->Map())
			{
				std::memcpy(out.data(), p, out.size());
			}
			buf->Unmap();
			return out;
		};

		CookedIBL ibl;
		ibl.IrradianceSize = kIrradianceCubeSize;
		ibl.PrefilteredSize = kPrefilterCubeSize;
		ibl.PrefilteredMips = kPrefilterMips;
		ibl.BRDFLutSize = kBRDFLutSize;

		uint32_t b = 0;
		ibl.Irradiance.assign(6, std::vector<std::vector<uint8_t>>(1));
		for (uint32_t f = 0; f < 6; ++f)
		{
			ibl.Irradiance[f][0] = readBuf(m_ReadbackBuffers[b++], kIrradianceCubeSize, kIrradianceCubeSize);
		}
		ibl.Prefiltered.assign(6, std::vector<std::vector<uint8_t>>(kPrefilterMips));
		for (uint32_t f = 0; f < 6; ++f)
		{
			for (uint32_t m = 0; m < kPrefilterMips; ++m)
			{
				const uint32_t s = std::max(1u, kPrefilterCubeSize >> m);
				ibl.Prefiltered[f][m] = readBuf(m_ReadbackBuffers[b++], s, s);
			}
		}
		ibl.BRDFLut = readBuf(m_ReadbackBuffers[b++], kBRDFLutSize, kBRDFLutSize);

		m_ReadbackBuffers.clear(); // GPU done (this runs a frame after the readback submit retired)

		if (IBLCacheIO::Save(m_PendingEnvHash, ibl))
		{
			SS_CORE_INFO("[IBL] wrote map cache (hash={:016x})", m_PendingEnvHash);
		}
	}
}
