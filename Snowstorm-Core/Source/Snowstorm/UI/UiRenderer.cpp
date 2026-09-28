#include "UiRenderer.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/JobSystem.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

#include <RmlUi/Core/Matrix4.h>
#include <RmlUi/Core/Vertex.h>

#include <stb_image.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>

namespace Snowstorm
{
	namespace
	{
		constexpr uint32_t kSetIndex = 1;
		constexpr uint32_t kVerticesBinding = 4; // StructuredBuffer<uint> UiVertices : t4
		constexpr uint32_t kSamplerBinding = 5;  // SamplerState UiSampler : s5
		constexpr uint32_t kWordsPerVertex = 5;
		constexpr size_t kInitialBytes = 64 * 1024;
		// Decoded images uploaded per frame. Each is a staging copy of a full mip chain; a screen that opens
		// with a dozen pictures fills in over a few frames instead of stalling one.
		constexpr int kUploadsPerFrame = 2;

		// Mirrors UiPush in Ui.hlsli field-for-field.
		struct UiPushConstants
		{
			float Transform[16]; // four columns
			float Translation[2];
			uint32_t TextureIndex;
			uint32_t Flags;
		};
		static_assert(sizeof(UiPushConstants) == 80, "UiPushConstants must match UiPush in Ui.hlsli (80 bytes)");

		uint32_t Word(const float f)
		{
			uint32_t w;
			std::memcpy(&w, &f, sizeof(w));
			return w;
		}

		// RmlUi composes premultiplied colour everywhere and expects images to arrive that way too.
		void Premultiply(std::vector<uint8_t>& rgba)
		{
			for (size_t i = 0; i + 3 < rgba.size(); i += 4)
			{
				const uint32_t a = rgba[i + 3];
				rgba[i + 0] = static_cast<uint8_t>((rgba[i + 0] * a + 127) / 255);
				rgba[i + 1] = static_cast<uint8_t>((rgba[i + 1] * a + 127) / 255);
				rgba[i + 2] = static_cast<uint8_t>((rgba[i + 2] * a + 127) / 255);
			}
		}

		void Store(std::array<float, 16>& out, const Rml::Matrix4f& m)
		{
			std::memcpy(out.data(), m.data(), sizeof(float) * 16);
		}
	}

	UiRenderer::UiRenderer() = default;

	UiRenderer::~UiRenderer()
	{
		// RmlUi releases what it compiled before shutdown; anything left is a leak on its side, not a crash
		// on ours, so it is only reported.
		if (!m_Textures.empty())
		{
			SS_CORE_WARN("UiRenderer: {} textures still alive at shutdown", m_Textures.size());
		}
		for (TextureEntry* e : m_Textures)
		{
			delete e;
		}
	}

	// ---- geometry

	Rml::CompiledGeometryHandle UiRenderer::CompileGeometry(const Rml::Span<const Rml::Vertex> vertices,
	                                                        const Rml::Span<const int> indices)
	{
		auto* g = new Geometry{};
		g->Vertices.assign(vertices.begin(), vertices.end());
		g->Indices.assign(indices.begin(), indices.end());
		return reinterpret_cast<Rml::CompiledGeometryHandle>(g);
	}

	void UiRenderer::ReleaseGeometry(const Rml::CompiledGeometryHandle geometry)
	{
		auto* g = reinterpret_cast<Geometry*>(geometry);
		m_Uploaded.erase(g);
		delete g;
	}

	void UiRenderer::RenderGeometry(const Rml::CompiledGeometryHandle geometry, const Rml::Vector2f translation,
	                                const Rml::TextureHandle texture)
	{
		const auto* g = reinterpret_cast<const Geometry*>(geometry);
		if (g == nullptr || g->Indices.empty())
		{
			return;
		}

		DrawCommand cmd{};
		if (texture != 0)
		{
			const auto* t = reinterpret_cast<const TextureEntry*>(texture);
			if (!t->Texture)
			{
				return; // still decoding, or failed: drawn once it exists
			}
			cmd.Textured = true;
			cmd.TextureIndex = t->Bindless;
		}

		// Copy the geometry into this frame once, however many times it is drawn (a repeated decorator).
		auto [it, inserted] = m_Uploaded.try_emplace(g, 0u, 0u);
		if (inserted)
		{
			const uint32_t base = static_cast<uint32_t>(m_FrameVertices.size() / kWordsPerVertex);
			m_FrameVertices.reserve(m_FrameVertices.size() + g->Vertices.size() * kWordsPerVertex);
			for (const Rml::Vertex& v : g->Vertices)
			{
				const Rml::ColourbPremultiplied& c = v.colour;
				m_FrameVertices.push_back(Word(v.position.x));
				m_FrameVertices.push_back(Word(v.position.y));
				m_FrameVertices.push_back(static_cast<uint32_t>(c.red) | static_cast<uint32_t>(c.green) << 8 |
				                          static_cast<uint32_t>(c.blue) << 16 | static_cast<uint32_t>(c.alpha) << 24);
				m_FrameVertices.push_back(Word(v.tex_coord.x));
				m_FrameVertices.push_back(Word(v.tex_coord.y));
			}
			const uint32_t first = static_cast<uint32_t>(m_FrameIndices.size());
			m_FrameIndices.reserve(m_FrameIndices.size() + g->Indices.size());
			for (const int i : g->Indices)
			{
				m_FrameIndices.push_back(base + static_cast<uint32_t>(i));
			}
			it->second = {first, static_cast<uint32_t>(g->Indices.size())};
		}

		cmd.FirstIndex = it->second.first;
		cmd.IndexCount = it->second.second;
		cmd.Translation = translation;
		cmd.Scissor = m_ScissorEnabled;
		cmd.ScissorRect = m_ScissorRect;
		cmd.Transform = m_Combined;
		m_Commands.push_back(cmd);
	}

	// ---- textures

	Rml::TextureHandle UiRenderer::LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source)
	{
		const std::filesystem::path path(std::u8string(source.begin(), source.end()));
		int w = 0;
		int h = 0;
		int comp = 0;
		if (stbi_info(path.string().c_str(), &w, &h, &comp) == 0)
		{
			SS_CORE_WARN("UiRenderer: cannot read image '{}'", source);
			return 0;
		}
		textureDimensions = {w, h};

		auto* e = new TextureEntry{};
		e->Source = source;
		e->Decoding = Application::Get().GetServiceManager().GetService<JobSystem>().Submit(
		    [path]() -> std::optional<CookedTexture>
		    {
			    std::optional<CookedTexture> cooked = Texture::DecodeCPU(path, AssetHandle{0}, 0);
			    if (cooked)
			    {
				    for (std::vector<uint8_t>& level : cooked->Levels)
				    {
					    Premultiply(level);
				    }
			    }
			    return cooked;
		    });
		m_Textures.insert(e);
		return reinterpret_cast<Rml::TextureHandle>(e);
	}

	Rml::TextureHandle UiRenderer::GenerateTexture(const Rml::Span<const Rml::byte> source,
	                                               const Rml::Vector2i sourceDimensions)
	{
		CookedTexture cooked{};
		cooked.Width = static_cast<uint32_t>(sourceDimensions.x);
		cooked.Height = static_cast<uint32_t>(sourceDimensions.y);
		cooked.Levels.emplace_back(source.begin(), source.end()); // already premultiplied (font atlases)

		auto* e = new TextureEntry{};
		e->Source = "(generated)";
		e->Texture = Texture::CreateFromPixels(cooked, false, "RmlGenerated");
		if (!e->Texture)
		{
			delete e;
			return 0;
		}
		e->Bindless = e->Texture->GetDefaultView()->GetGlobalBindlessIndex();
		m_Textures.insert(e);
		return reinterpret_cast<Rml::TextureHandle>(e);
	}

	void UiRenderer::ReleaseTexture(const Rml::TextureHandle texture)
	{
		auto* e = reinterpret_cast<TextureEntry*>(texture);
		m_Textures.erase(e);
		// A decode still running finishes into a future nobody reads; the texture Ref goes through the
		// backend's deferred release, so a frame in flight that samples it is unaffected.
		delete e;
	}

	void UiRenderer::PumpDecodes()
	{
		int uploads = 0;
		for (TextureEntry* e : m_Textures)
		{
			if (!e->Decoding || e->Decoding->wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			{
				continue;
			}
			std::optional<CookedTexture> cooked = e->Decoding->get();
			e->Decoding.reset();
			if (!cooked)
			{
				e->Failed = true;
				SS_CORE_WARN("UiRenderer: could not decode '{}'", e->Source);
				continue;
			}
			e->Texture = Texture::CreateFromPixels(*cooked, false, "RmlImage");
			if (e->Texture)
			{
				e->Bindless = e->Texture->GetDefaultView()->GetGlobalBindlessIndex();
			}
			if (++uploads >= kUploadsPerFrame)
			{
				break;
			}
		}
	}

	size_t UiRenderer::PendingTextures() const
	{
		return static_cast<size_t>(std::count_if(m_Textures.begin(), m_Textures.end(),
		                                          [](const TextureEntry* e) { return e->Decoding.has_value(); }));
	}

	// ---- state

	void UiRenderer::EnableScissorRegion(const bool enable)
	{
		m_ScissorEnabled = enable;
	}

	void UiRenderer::SetScissorRegion(const Rml::Rectanglei region)
	{
		m_ScissorRect = region;
	}

	void UiRenderer::SetTransform(const Rml::Matrix4f* transform)
	{
		m_HasTransform = transform != nullptr;
		if (transform)
		{
			Store(m_ElementTransform, *transform);
		}
		UpdateTransform();
	}

	void UiRenderer::UpdateTransform()
	{
		// Pixel space (top-left origin, y down) to Vulkan clip space, whose y = -1 is the TOP of the swapchain
		// image: so the ortho's "bottom" is 0 and its "top" is the height.
		const Rml::Matrix4f projection = Rml::Matrix4f::ProjectOrtho(0.0f, static_cast<float>(m_Width), 0.0f,
		                                                             static_cast<float>(m_Height), -10000.0f, 10000.0f);
		if (m_HasTransform)
		{
			Rml::Matrix4f element;
			std::memcpy(element.data(), m_ElementTransform.data(), sizeof(float) * 16);
			Store(m_Combined, projection * element);
		}
		else
		{
			Store(m_Combined, projection);
		}
	}

	// ---- the frame

	void UiRenderer::BeginFrame(const uint32_t width, const uint32_t height)
	{
		m_Width = width;
		m_Height = height;
		m_FrameVertices.clear();
		m_FrameIndices.clear();
		m_Commands.clear();
		m_Uploaded.clear();
		m_ScissorEnabled = false;
		m_HasTransform = false;
		UpdateTransform();
		PumpDecodes();
	}

	void UiRenderer::EnsureSampler()
	{
		if (m_Sampler)
		{
			return;
		}
		// Trilinear and clamped: pictures are drawn smaller than they were rendered, which without mips
		// shimmers, and a clamp keeps a 9-slice from bleeding its opposite edge in.
		SamplerDesc s{};
		s.MinFilter = Filter::Linear;
		s.MagFilter = Filter::Linear;
		s.MipmapMode = SamplerMipmapMode::Linear;
		s.AddressU = SamplerAddressMode::ClampToEdge;
		s.AddressV = SamplerAddressMode::ClampToEdge;
		s.AddressW = SamplerAddressMode::ClampToEdge;
		s.EnableAnisotropy = false;
		s.DebugName = "UiSampler";
		m_Sampler = Sampler::Create(s);
		SS_CORE_ASSERT(m_Sampler, "Failed to create UI sampler");
	}

	void UiRenderer::EnsurePipeline(const PixelFormat colorFormat)
	{
		if (m_Pipeline && m_ColorFormat == colorFormat)
		{
			return;
		}
		Ref<Shader> shader = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load(
		    "Engine/Shaders/Ui.vert.hlsl", "Engine/Shaders/Ui.frag.hlsl");
		SS_CORE_ASSERT(shader, "Failed to load UI shader");
		if (!shader->IsReady())
		{
			return; // async compile; Draw retries next frame
		}

		PipelineDesc p{};
		p.Type = PipelineType::Graphics;
		p.Shader = shader;
		p.ColorFormats = {colorFormat};
		p.DepthFormat = PixelFormat::Unknown;
		p.Raster.Cull = CullMode::None;
		p.DepthStencil.EnableDepthTest = false;
		p.DepthStencil.EnableDepthWrite = false;
		p.Blend.Attachments = {PipelineBlendAttachment{.EnableBlend = true, .Mode = BlendMode::Premultiplied}};
		p.PushConstants = {{.Offset = 0, .Size = sizeof(UiPushConstants), .Stages = ShaderStage::AllGraphics}};
		p.DebugName = "UiPipeline";
		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create UI pipeline");
		m_ColorFormat = colorFormat;
	}

	UiRenderer::FrameResources& UiRenderer::EnsureFrameResources(const uint32_t frameIndex, const size_t vertexBytes,
	                                                             const size_t indexBytes)
	{
		const uint32_t frames = Renderer::GetFramesInFlight();
		if (m_Frames.size() < frames)
		{
			m_Frames.resize(frames);
		}
		FrameResources& fr = m_Frames[frameIndex];
		if (!fr.Set)
		{
			const auto& layouts = m_Pipeline->GetSetLayouts();
			SS_CORE_ASSERT(layouts.size() > kSetIndex && layouts[kSetIndex], "UI pipeline missing set=1 layout");
			DescriptorSetDesc d{};
			d.DebugName = "Ui_Set1";
			fr.Set = DescriptorSet::Create(layouts[kSetIndex], d);
		}
		// Grown by doubling, never shrunk. The slot's previous buffer may still be read by its last submission;
		// dropping the Ref hands it to the backend's deferred release, as for any buffer.
		const auto grow = [](size_t have, const size_t need)
		{
			have = std::max(have, kInitialBytes);
			while (have < need)
			{
				have *= 2;
			}
			return have;
		};
		if (fr.VertexCapacity < vertexBytes)
		{
			fr.VertexCapacity = grow(fr.VertexCapacity, vertexBytes);
			fr.Vertices = Buffer::Create(fr.VertexCapacity, BufferUsage::Storage, nullptr, true, "UiVertices");
		}
		if (fr.IndexCapacity < indexBytes)
		{
			fr.IndexCapacity = grow(fr.IndexCapacity, indexBytes);
			fr.Indices = Buffer::Create(fr.IndexCapacity, BufferUsage::Index, nullptr, true, "UiIndices");
		}
		return fr;
	}

	void UiRenderer::Draw(CommandContext& ctx, const uint32_t frameIndex, const PixelFormat colorFormat)
	{
		if (m_Commands.empty() || m_Width == 0 || m_Height == 0)
		{
			return;
		}
		EnsureSampler();
		EnsurePipeline(colorFormat);
		if (!m_Pipeline)
		{
			return;
		}

		const size_t vertexBytes = m_FrameVertices.size() * sizeof(uint32_t);
		const size_t indexBytes = m_FrameIndices.size() * sizeof(uint32_t);
		FrameResources& fr = EnsureFrameResources(frameIndex, vertexBytes, indexBytes);
		fr.Vertices->SetData(m_FrameVertices.data(), vertexBytes, 0);
		fr.Indices->SetData(m_FrameIndices.data(), indexBytes, 0);
		fr.Set->SetBuffer(kVerticesBinding, {.Buffer = fr.Vertices, .Offset = 0, .Range = 0});
		fr.Set->SetSampler(kSamplerBinding, m_Sampler);
		fr.Set->Commit();

		ctx.BindPipeline(m_Pipeline);
		ctx.BindDescriptorSet(fr.Set, kSetIndex);
		ctx.BindGlobalResources(); // set 3: the bindless textures
		ctx.SetViewport(0.0f, 0.0f, static_cast<float>(m_Width), static_cast<float>(m_Height));

		for (const DrawCommand& cmd : m_Commands)
		{
			if (cmd.Scissor)
			{
				const int x0 = std::clamp(cmd.ScissorRect.Left(), 0, static_cast<int>(m_Width));
				const int y0 = std::clamp(cmd.ScissorRect.Top(), 0, static_cast<int>(m_Height));
				const int x1 = std::clamp(cmd.ScissorRect.Right(), 0, static_cast<int>(m_Width));
				const int y1 = std::clamp(cmd.ScissorRect.Bottom(), 0, static_cast<int>(m_Height));
				if (x1 <= x0 || y1 <= y0)
				{
					continue; // clipped away entirely
				}
				ctx.SetScissor(static_cast<uint32_t>(x0), static_cast<uint32_t>(y0), static_cast<uint32_t>(x1 - x0),
				               static_cast<uint32_t>(y1 - y0));
			}
			else
			{
				ctx.SetScissor(0, 0, m_Width, m_Height);
			}

			UiPushConstants push{};
			std::memcpy(push.Transform, cmd.Transform.data(), sizeof(push.Transform));
			push.Translation[0] = cmd.Translation.x;
			push.Translation[1] = cmd.Translation.y;
			push.TextureIndex = cmd.TextureIndex;
			push.Flags = cmd.Textured ? 1u : 0u;
			ctx.PushConstants(&push, sizeof(push), 0);
			ctx.DrawIndexed(fr.Indices, cmd.IndexCount, 1, cmd.FirstIndex, 0, 0);
		}
	}
}
