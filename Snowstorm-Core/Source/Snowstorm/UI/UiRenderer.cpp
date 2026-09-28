#include "UiRenderer.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/JobSystem.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Dictionary.h>
#include <RmlUi/Core/Matrix4.h>
#include <RmlUi/Core/Vertex.h>

#include <stb_image.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace Snowstorm
{
	namespace
	{
		constexpr uint32_t kSetIndex = 1;
		constexpr uint32_t kVerticesBinding = 4;  // StructuredBuffer<uint> UiVertices : t4
		constexpr uint32_t kSamplerBinding = 5;   // SamplerState UiSampler / UiPostSampler : s5
		constexpr uint32_t kGradientsBinding = 6; // StructuredBuffer<UiGradient> UiGradients : t6
		constexpr uint32_t kWordsPerVertex = 5;
		constexpr size_t kInitialBytes = 64 * 1024;
		// Decoded images uploaded per frame. Each is a staging copy of a full mip chain; a screen that opens with
		// a dozen pictures fills in over a few frames instead of stalling one.
		constexpr int kUploadsPerFrame = 2;
		// How long a released image waits to be asked for again before it is freed.
		constexpr uint64_t kParkedFrames = 120;
		// Downsample levels a blur may use: sigma halves with each, and 1/32 of the window is already a smear.
		constexpr uint32_t kLevels = 6;
		// A blur stays at the resolution where sigma is at most this many taps (the shader takes 3 sigma).
		constexpr float kMaxSigmaTaps = 8.0f;
		constexpr PixelFormat kLayerFormat = PixelFormat::RGBA8_UNorm;

		// UiPost.frag's modes.
		constexpr uint32_t kPostCopy = 0;
		constexpr uint32_t kPostOpacity = 1;
		constexpr uint32_t kPostColorMatrix = 2;
		constexpr uint32_t kPostBlur = 3;
		constexpr uint32_t kPostShadow = 4;
		constexpr uint32_t kPostOver = 5;
		constexpr uint32_t kPostMaskMultiply = 6;

		// Ui.hlsli's flags.
		constexpr uint32_t kFlagTextured = 1;
		constexpr uint32_t kFlagGradient = 2;
		constexpr uint32_t kFlagMaskWrite = 4;

		// Mirrors UiPush in Ui.hlsli field-for-field.
		struct UiPushConstants
		{
			float Transform[16]; // four columns
			float Translation[2];
			uint32_t TextureIndex;
			uint32_t Flags;
			uint32_t GradientIndex;
			uint32_t MaskIndex;
			float MaskValue;
			uint32_t Pad;
			float UvScale[2];
		};
		static_assert(sizeof(UiPushConstants) == 104, "UiPushConstants must match UiPush in Ui.hlsli (104 bytes)");

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

		// A scratch slot in 0..2 that is neither `a` nor `b` (-1 for none).
		int FreeSlot(const int a, const int b)
		{
			for (int s = 0; s < 3; ++s)
			{
				if (s != a && s != b)
				{
					return s;
				}
			}
			return 0;
		}

		std::vector<RenderGraph::ResourceAccess> Sampled(const std::vector<Ref<Texture>>& textures)
		{
			std::vector<RenderGraph::ResourceAccess> out;
			out.reserve(textures.size());
			for (const Ref<Texture>& t : textures)
			{
				if (t && std::none_of(out.begin(), out.end(), [&t](const auto& r) { return r.Texture == t; }))
				{
					out.push_back({.Texture = t, .State = RenderGraph::AccessState::Sampled});
				}
			}
			return out;
		}

		// A 3x3 colour matrix (rows are outputs) and a translation, packed as PostPush::Matrix: per INPUT
		// channel, what it contributes to r', g', b', then that output channel's translation.
		std::array<float, 12> Pack(const float m[3][3], const float t[3])
		{
			std::array<float, 12> out{};
			for (int in = 0; in < 3; ++in)
			{
				out[in * 4 + 0] = m[0][in];
				out[in * 4 + 1] = m[1][in];
				out[in * 4 + 2] = m[2][in];
				out[in * 4 + 3] = t[in];
			}
			return out;
		}

		// The CSS Filter Effects colour filters, as matrices on straight colour. Returns false for any other name.
		bool ColourMatrixFor(const Rml::String& name, const float v, std::array<float, 12>& out)
		{
			float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
			float t[3] = {0, 0, 0};
			if (name == "brightness")
			{
				for (int i = 0; i < 3; ++i)
				{
					m[i][i] = v;
				}
			}
			else if (name == "contrast")
			{
				for (int i = 0; i < 3; ++i)
				{
					m[i][i] = v;
					t[i] = 0.5f - 0.5f * v;
				}
			}
			else if (name == "invert")
			{
				const float a = std::clamp(v, 0.0f, 1.0f);
				for (int i = 0; i < 3; ++i)
				{
					m[i][i] = 1.0f - 2.0f * a;
					t[i] = a;
				}
			}
			else if (name == "grayscale")
			{
				const float s = 1.0f - std::clamp(v, 0.0f, 1.0f);
				const float g[3][3] = {{0.2126f + 0.7874f * s, 0.7152f - 0.7152f * s, 0.0722f - 0.0722f * s},
				                       {0.2126f - 0.2126f * s, 0.7152f + 0.2848f * s, 0.0722f - 0.0722f * s},
				                       {0.2126f - 0.2126f * s, 0.7152f - 0.7152f * s, 0.0722f + 0.9278f * s}};
				std::memcpy(m, g, sizeof(m));
			}
			else if (name == "sepia")
			{
				const float s = 1.0f - std::clamp(v, 0.0f, 1.0f);
				const float g[3][3] = {{0.393f + 0.607f * s, 0.769f - 0.769f * s, 0.189f - 0.189f * s},
				                       {0.349f - 0.349f * s, 0.686f + 0.314f * s, 0.168f - 0.168f * s},
				                       {0.272f - 0.272f * s, 0.534f - 0.534f * s, 0.131f + 0.869f * s}};
				std::memcpy(m, g, sizeof(m));
			}
			else if (name == "saturate")
			{
				const float s = v;
				const float g[3][3] = {{0.213f + 0.787f * s, 0.715f - 0.715f * s, 0.072f - 0.072f * s},
				                       {0.213f - 0.213f * s, 0.715f + 0.285f * s, 0.072f - 0.072f * s},
				                       {0.213f - 0.213f * s, 0.715f - 0.715f * s, 0.072f + 0.928f * s}};
				std::memcpy(m, g, sizeof(m));
			}
			else if (name == "hue-rotate")
			{
				const float c = std::cos(v);
				const float s = std::sin(v);
				const float g[3][3] = {
				    {0.213f + c * 0.787f - s * 0.213f, 0.715f - c * 0.715f - s * 0.715f, 0.072f - c * 0.072f + s * 0.928f},
				    {0.213f - c * 0.213f + s * 0.143f, 0.715f + c * 0.285f + s * 0.140f, 0.072f - c * 0.072f - s * 0.283f},
				    {0.213f - c * 0.213f - s * 0.787f, 0.715f - c * 0.715f + s * 0.715f, 0.072f + c * 0.928f + s * 0.072f}};
				std::memcpy(m, g, sizeof(m));
			}
			else
			{
				return false;
			}
			out = Pack(m, t);
			return true;
		}
	}

	UiRenderer::UiRenderer()
	{
		static_assert(sizeof(PostPush) == 128, "PostPush must match UiPostPush in UiPost.hlsli (128 bytes)");
		static_assert(sizeof(GradientGpu) == 352, "GradientGpu must match UiGradient in Ui.hlsli (352 bytes)");
	}

	UiRenderer::~UiRenderer()
	{
		// RmlUi releases what it compiled before shutdown; anything left is a leak on its side, not a crash on
		// ours, so it is only reported.
		if (!m_Textures.empty())
		{
			SS_CORE_WARN("UiRenderer: {} textures still alive at shutdown", m_Textures.size());
		}
		for (TextureEntry* e : m_Textures)
		{
			delete e;
		}
		for (auto& [source, parked] : m_Parked)
		{
			delete parked.second;
		}
	}

	// ---- geometry

	Rml::CompiledGeometryHandle UiRenderer::CompileGeometry(const Rml::Span<const Rml::Vertex> vertices,
	                                                        const Rml::Span<const int> indices)
	{
		auto* g = new Geometry{};
		g->Vertices.assign(vertices.begin(), vertices.end());
		g->Indices.assign(indices.begin(), indices.end());
		if (!g->Vertices.empty())
		{
			Rml::Vector2f lo = g->Vertices[0].position;
			Rml::Vector2f hi = lo;
			for (const Rml::Vertex& v : g->Vertices)
			{
				lo = Rml::Math::Min(lo, v.position);
				hi = Rml::Math::Max(hi, v.position);
			}
			g->Size = hi - lo;
		}
		return reinterpret_cast<Rml::CompiledGeometryHandle>(g);
	}

	void UiRenderer::ReleaseGeometry(const Rml::CompiledGeometryHandle geometry)
	{
		auto* g = reinterpret_cast<Geometry*>(geometry);
		m_Uploaded.erase(g);
		delete g;
	}

	UiRenderer::DrawCommand UiRenderer::MakeDraw(const Geometry* g, const Rml::Vector2f translation)
	{
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

		DrawCommand d;
		d.FirstIndex = it->second.first;
		d.IndexCount = it->second.second;
		d.Translation = translation;
		d.Scissor = m_ScissorEnabled;
		d.ScissorRect = m_ScissorRect;
		d.Transform = m_Combined;
		d.Mask = m_ClipEnabled ? m_MaskSlot : -1;
		return d;
	}

	UiRenderer::SegmentOp& UiRenderer::CurrentSegment()
	{
		if (m_Ops.empty() || !std::holds_alternative<SegmentOp>(m_Ops.back()) ||
		    std::get<SegmentOp>(m_Ops.back()).Layer != TopLayer())
		{
			m_Ops.emplace_back(SegmentOp{TopLayer(), false, {}});
		}
		return std::get<SegmentOp>(m_Ops.back());
	}

	void UiRenderer::Record(const Geometry* g, const Rml::Vector2f translation, const Rml::TextureHandle texture,
	                        const int gradient)
	{
		if (g == nullptr || g->Indices.empty())
		{
			return;
		}
		uint32_t bindless = 0;
		if (texture != 0)
		{
			const auto* t = reinterpret_cast<const TextureEntry*>(texture);
			if (!t->Texture)
			{
				return; // still decoding, or failed: drawn once it exists
			}
			bindless = t->Bindless;
		}
		DrawCommand d = MakeDraw(g, translation);
		d.Textured = texture != 0;
		d.TextureIndex = bindless;
		if (texture != 0)
		{
			// A baked layer smaller than the quad showing it was clipped to the window when RmlUi baked it (it
			// warns "Results may be clipped"): draw what exists at 1:1 rather than stretch it over the quad.
			if (const auto* t = reinterpret_cast<const TextureEntry*>(texture); t->Saved)
			{
				d.UvScale.x = std::max(g->Size.x / static_cast<float>(t->Saved->Width), 1.0f);
				d.UvScale.y = std::max(g->Size.y / static_cast<float>(t->Saved->Height), 1.0f);
			}
		}
		d.Gradient = gradient;
		CurrentSegment().Draws.push_back(d);
	}

	void UiRenderer::RenderGeometry(const Rml::CompiledGeometryHandle geometry, const Rml::Vector2f translation,
	                                const Rml::TextureHandle texture)
	{
		Record(reinterpret_cast<const Geometry*>(geometry), translation, texture, -1);
	}

	// ---- textures

	Rml::TextureHandle UiRenderer::LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source)
	{
		if (const auto parked = m_Parked.find(source); parked != m_Parked.end())
		{
			TextureEntry* e = parked->second.second;
			m_Parked.erase(parked);
			m_Textures.insert(e);
			textureDimensions = e->Dimensions;
			return reinterpret_cast<Rml::TextureHandle>(e);
		}
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
		e->File = true;
		e->Dimensions = textureDimensions;
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
		if (e->Clipped)
		{
			--m_ClippedBakes;
		}
		if (e->File)
		{
			// Kept a while in case RmlUi asks for it again (LoadTexture); BeginFrame retires what nobody wants.
			if (m_Parked.try_emplace(e->Source, m_FrameCounter, e).second)
			{
				return;
			}
		}
		// A decode still running finishes into a future nobody reads. The image itself waits out the frames in
		// flight (Retire), because the backend destroys an image the moment its last Ref goes.
		if (e->Saved)
		{
			Retire(*e->Saved);
		}
		Retire(e->Texture);
		delete e;
	}

	void UiRenderer::Retire(const Ref<Texture>& texture)
	{
		if (texture)
		{
			m_Retired.emplace_back(m_FrameCounter, texture);
		}
	}

	void UiRenderer::Retire(const Target& target)
	{
		Retire(target.Texture);
		for (const Ref<RenderTarget>& rt : {target.Clear, target.ClearOne, target.Load})
		{
			if (rt)
			{
				m_Retired.emplace_back(m_FrameCounter, rt);
			}
		}
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
		// Pixel space (top-left origin, y down) to Vulkan clip space, whose y = -1 is the TOP of the target: so
		// the ortho's "bottom" is 0 and its "top" is the height.
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

	// ---- clip masks

	void UiRenderer::EnableClipMask(const bool enable)
	{
		m_ClipEnabled = enable;
	}

	void UiRenderer::RenderToClipMask(const Rml::ClipMaskOperation operation, const Rml::CompiledGeometryHandle geometry,
	                                  const Rml::Vector2f translation)
	{
		const auto* g = reinterpret_cast<const Geometry*>(geometry);
		if (g == nullptr)
		{
			return;
		}
		MaskOp m;
		m.Operation = operation;
		m.Draw = MakeDraw(g, translation);
		m.Draw.Mask = -1;
		// Rotate through three masks so a change never writes the mask an earlier draw reads: the replay runs in
		// order, but each draw names the physical mask it saw.
		if (operation == Rml::ClipMaskOperation::Intersect && m_MaskSlot >= 0)
		{
			m.Previous = m_MaskSlot;
			m.Scratch = FreeSlot(m_MaskSlot, -1);
			m.Into = FreeSlot(m_MaskSlot, m.Scratch);
		}
		else
		{
			// Intersecting with no mask yet is just setting one.
			if (operation == Rml::ClipMaskOperation::Intersect)
			{
				m.Operation = Rml::ClipMaskOperation::Set;
			}
			m.Into = FreeSlot(m_MaskSlot, -1);
		}
		m_MaskSlot = m.Into;
		m_Ops.emplace_back(std::move(m));
	}

	uint32_t UiRenderer::MaskBindless(const int slot) const
	{
		return slot < 0 ? 0 : m_Masks[static_cast<size_t>(slot)].Bindless;
	}

	// ---- layers

	Rml::LayerHandle UiRenderer::PushLayer()
	{
		const auto index = static_cast<uint32_t>(m_LayerStack.size());
		Layer(index); // made now, while nothing holds a pointer into the layer list
		m_LayerStack.push_back(index);
		m_Ops.emplace_back(SegmentOp{index, true, {}}); // a new layer starts transparent
		return static_cast<Rml::LayerHandle>(index);
	}

	void UiRenderer::PopLayer()
	{
		if (m_LayerStack.size() > 1)
		{
			m_LayerStack.pop_back();
		}
	}

	void UiRenderer::CompositeLayers(const Rml::LayerHandle source, const Rml::LayerHandle destination,
	                                 const Rml::BlendMode blendMode, const Rml::Span<const Rml::CompiledFilterHandle> filters)
	{
		CompositeOp c;
		c.Source = static_cast<uint32_t>(source);
		c.Destination = static_cast<uint32_t>(destination);
		Layer(c.Source);
		Layer(c.Destination);
		c.Blend = blendMode;
		for (const Rml::CompiledFilterHandle f : filters)
		{
			if (f != 0)
			{
				// By value: a box-shadow bake releases its blur filter right after this call, long before the
				// frame's passes are built.
				c.Filters.push_back(*reinterpret_cast<const Filter*>(f));
			}
		}
		c.Mask = m_ClipEnabled ? m_MaskSlot : -1;
		c.Scissor = m_ScissorEnabled;
		c.ScissorRect = m_ScissorRect;
		m_Ops.emplace_back(std::move(c));
	}

	Rml::TextureHandle UiRenderer::SaveLayerAsTexture()
	{
		// The saved region is the active scissor, which is how RmlUi sizes what it bakes (a box-shadow's extent).
		const int w = static_cast<int>(m_Width);
		const int h = static_cast<int>(m_Height);
		Rml::Rectanglei r = m_ScissorEnabled ? m_ScissorRect : Rml::Rectanglei::FromSize({w, h});
		const int x0 = std::clamp(r.Left(), 0, w);
		const int y0 = std::clamp(r.Top(), 0, h);
		const int x1 = std::clamp(r.Right(), x0 + 1, std::max(w, x0 + 1));
		const int y1 = std::clamp(r.Bottom(), y0 + 1, std::max(h, y0 + 1));
		r = Rml::Rectanglei::FromCorners({x0, y0}, {x1, y1});

		auto* e = new TextureEntry{};
		e->Source = "(saved layer)";
		e->Saved = MakeTarget(static_cast<uint32_t>(r.Width()), static_cast<uint32_t>(r.Height()), "RmlSavedLayer");
		e->Texture = e->Saved->Texture;
		e->Bindless = e->Saved->Bindless;
		// RmlUi clamps the region it asks for to the window, so one that reaches the edge was probably cut short.
		e->Clipped = r.Right() >= w || r.Bottom() >= h;
		m_ClippedBakes += e->Clipped ? 1 : 0;
		m_Textures.insert(e);
		m_Ops.emplace_back(SaveOp{TopLayer(), r, e});
		return reinterpret_cast<Rml::TextureHandle>(e);
	}

	// ---- filters

	Rml::CompiledFilterHandle UiRenderer::CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters)
	{
		auto* f = new Filter{};
		if (name == "opacity")
		{
			f->Type = Filter::Kind::Opacity;
			f->Value = Rml::Get(parameters, "value", 1.0f);
		}
		else if (name == "blur")
		{
			f->Type = Filter::Kind::Blur;
			f->Sigma = Rml::Get(parameters, "sigma", 0.0f);
		}
		else if (name == "drop-shadow")
		{
			f->Type = Filter::Kind::DropShadow;
			f->Sigma = Rml::Get(parameters, "sigma", 0.0f);
			const Rml::Vector2f offset = Rml::Get(parameters, "offset", Rml::Vector2f(0.0f));
			f->Offset = {offset.x, offset.y};
			const Rml::Colourb c = Rml::Get(parameters, "color", Rml::Colourb(0, 0, 0, 255));
			const float a = c.alpha / 255.0f;
			f->Colour = {c.red / 255.0f * a, c.green / 255.0f * a, c.blue / 255.0f * a, a};
		}
		else if (ColourMatrixFor(name, Rml::Get(parameters, "value", 1.0f), f->Matrix))
		{
			f->Type = Filter::Kind::ColorMatrix;
		}
		else
		{
			SS_CORE_WARN("UiRenderer: filter '{}' is not supported", name);
			delete f;
			return 0;
		}
		return reinterpret_cast<Rml::CompiledFilterHandle>(f);
	}

	void UiRenderer::ReleaseFilter(const Rml::CompiledFilterHandle filter)
	{
		delete reinterpret_cast<Filter*>(filter);
	}

	// ---- shaders: gradients

	Rml::CompiledShaderHandle UiRenderer::CompileShader(const Rml::String& name, const Rml::Dictionary& parameters)
	{
		auto* g = new GradientGpu{};
		if (name == "linear-gradient")
		{
			g->Kind = 0;
			const Rml::Vector2f p0 = Rml::Get(parameters, "p0", Rml::Vector2f(0.0f));
			const Rml::Vector2f p1 = Rml::Get(parameters, "p1", Rml::Vector2f(0.0f));
			g->P0[0] = p0.x;
			g->P0[1] = p0.y;
			g->P1[0] = p1.x;
			g->P1[1] = p1.y;
		}
		else if (name == "radial-gradient")
		{
			g->Kind = 1;
			const Rml::Vector2f center = Rml::Get(parameters, "center", Rml::Vector2f(0.0f));
			const Rml::Vector2f radius = Rml::Get(parameters, "radius", Rml::Vector2f(1.0f));
			g->P0[0] = center.x;
			g->P0[1] = center.y;
			g->P1[0] = radius.x;
			g->P1[1] = radius.y;
		}
		else if (name == "conic-gradient")
		{
			g->Kind = 2;
			const Rml::Vector2f center = Rml::Get(parameters, "center", Rml::Vector2f(0.0f));
			g->P0[0] = center.x;
			g->P0[1] = center.y;
			g->Angle = Rml::Get(parameters, "angle", 0.0f);
		}
		else
		{
			SS_CORE_WARN("UiRenderer: shader '{}' is not supported", name);
			delete g;
			return 0;
		}
		g->Repeating = Rml::Get(parameters, "repeating", false) ? 1u : 0u;
		if (const auto it = parameters.find("color_stop_list");
		    it != parameters.end() && it->second.GetType() == Rml::Variant::COLORSTOPLIST)
		{
			const Rml::ColorStopList& stops = it->second.GetReference<Rml::ColorStopList>();
			const size_t n = std::min<size_t>(stops.size(), 16);
			if (stops.size() > 16)
			{
				SS_CORE_WARN("UiRenderer: a gradient's {} colour stops cut to 16", stops.size());
			}
			for (size_t i = 0; i < n; ++i)
			{
				const Rml::ColourbPremultiplied& c = stops[i].color;
				g->Colors[i][0] = c.red / 255.0f;
				g->Colors[i][1] = c.green / 255.0f;
				g->Colors[i][2] = c.blue / 255.0f;
				g->Colors[i][3] = c.alpha / 255.0f;
				const Rml::NumericValue& p = stops[i].position;
				g->Positions[i] = p.unit == Rml::Unit::PERCENT ? p.number * 0.01f : p.number;
			}
			g->StopCount = static_cast<uint32_t>(n);
		}
		return reinterpret_cast<Rml::CompiledShaderHandle>(g);
	}

	void UiRenderer::RenderShader(const Rml::CompiledShaderHandle shader, const Rml::CompiledGeometryHandle geometry,
	                              const Rml::Vector2f translation, const Rml::TextureHandle texture)
	{
		const auto* g = reinterpret_cast<const GradientGpu*>(shader);
		if (g == nullptr)
		{
			return;
		}
		auto [it, inserted] = m_GradientSlots.try_emplace(g, static_cast<int>(m_FrameGradients.size()));
		if (inserted)
		{
			m_FrameGradients.push_back(*g);
		}
		Record(reinterpret_cast<const Geometry*>(geometry), translation, texture, it->second);
	}

	void UiRenderer::ReleaseShader(const Rml::CompiledShaderHandle shader)
	{
		auto* g = reinterpret_cast<GradientGpu*>(shader);
		m_GradientSlots.erase(g);
		delete g;
	}

	// ---- the frame

	bool UiRenderer::Ready(const PixelFormat swapFormat)
	{
		return EnsurePipelines(swapFormat);
	}

	void UiRenderer::BeginFrame(const uint32_t width, const uint32_t height)
	{
		// What was retired a full set of frames in flight ago can no longer be on the GPU.
		++m_FrameCounter;
		const uint64_t safe = static_cast<uint64_t>(Renderer::GetFramesInFlight()) + 1;
		std::erase_if(m_Retired, [this, safe](const auto& r) { return r.first + safe <= m_FrameCounter; });
		std::erase_if(m_Parked,
		              [this](const auto& entry)
		              {
			              const auto& [released, e] = entry.second;
			              if (released + kParkedFrames > m_FrameCounter)
			              {
				              return false;
			              }
			              Retire(e->Texture);
			              delete e;
			              return true;
		              });

		m_Width = width;
		m_Height = height;
		EnsureTargets(width, height);
		m_FrameVertices.clear();
		m_FrameIndices.clear();
		m_FrameGradients.clear();
		m_GradientSlots.clear();
		m_Uploaded.clear();
		m_Ops.clear();
		m_SavedThisFrame.clear();
		m_LayerStack.assign(1, 0);
		m_Ops.emplace_back(SegmentOp{0, true, {}}); // the base layer, cleared every frame
		// The render STATE is not reset: scissor, clip mask and transform stay as RmlUi last set them. Its
		// RenderManager caches that state and only calls this interface when it changes, so a reset here that it
		// never hears of leaves the two disagreeing, and a texture baked early in the next frame (a box-shadow
		// re-baked after a relayout) draws under a scissor or mask RmlUi believes is gone. The clip masks keep
		// their contents between frames for the same reason. Only the projection follows the window.
		UpdateTransform();
		PumpDecodes();
	}

	// ---- resources

	UiRenderer::Target UiRenderer::MakeTarget(const uint32_t width, const uint32_t height, const char* name)
	{
		Target t;
		t.Width = std::max(width, 1u);
		t.Height = std::max(height, 1u);
		TextureDesc d{};
		d.Format = kLayerFormat;
		d.Usage = TextureUsage::Sampled | TextureUsage::ColorAttachment;
		d.Width = t.Width;
		d.Height = t.Height;
		d.DebugName = name;
		t.Texture = Texture::Create(d);
		const Ref<TextureView> view = t.Texture->GetDefaultView();
		t.Bindless = view->GetGlobalBindlessIndex();

		RenderTargetAttachment a{};
		a.View = view;
		a.StoreOp = RenderTargetStoreOp::Store;
		RenderTargetDesc rd{};
		rd.Width = t.Width;
		rd.Height = t.Height;

		a.LoadOp = RenderTargetLoadOp::Clear;
		a.ClearColor = {0.0f, 0.0f, 0.0f, 0.0f};
		rd.ColorAttachments = {a};
		t.Clear = RenderTarget::Create(rd);
		a.ClearColor = {1.0f, 1.0f, 1.0f, 1.0f};
		rd.ColorAttachments = {a};
		t.ClearOne = RenderTarget::Create(rd);
		a.LoadOp = RenderTargetLoadOp::Load;
		rd.ColorAttachments = {a};
		t.Load = RenderTarget::Create(rd);
		return t;
	}

	void UiRenderer::EnsureTargets(const uint32_t width, const uint32_t height)
	{
		if (width != m_TargetWidth || height != m_TargetHeight)
		{
			// Frames in flight may still sample these; they wait out those frames (Retire).
			for (const Target& t : m_Layers)
			{
				Retire(t);
			}
			for (const Target& t : m_Masks)
			{
				Retire(t);
			}
			for (const auto& level : m_Scratch)
			{
				for (const Target& t : level)
				{
					Retire(t);
				}
			}
			m_Layers.clear();
			m_Masks = {};
			m_Scratch.clear();
			m_TargetWidth = width;
			m_TargetHeight = height;
		}
		m_Scratch.resize(kLevels); // never resized again: filter chains hold pointers into it
		Layer(0);
		for (size_t i = 0; i < m_Masks.size(); ++i)
		{
			if (!m_Masks[i].Texture)
			{
				m_Masks[i] = MakeTarget(width, height, "UiClipMask");
			}
		}
	}

	UiRenderer::Target& UiRenderer::Layer(const uint32_t index)
	{
		if (m_Layers.size() <= index)
		{
			m_Layers.resize(index + 1);
		}
		if (!m_Layers[index].Texture)
		{
			m_Layers[index] = MakeTarget(m_TargetWidth, m_TargetHeight, index == 0 ? "UiBaseLayer" : "UiLayer");
		}
		return m_Layers[index];
	}

	UiRenderer::Target& UiRenderer::Scratch(const uint32_t level, const int slot)
	{
		Target& t = m_Scratch[level][static_cast<size_t>(slot)];
		if (!t.Texture)
		{
			t = MakeTarget(std::max(m_TargetWidth >> level, 1u), std::max(m_TargetHeight >> level, 1u), "UiScratch");
		}
		return t;
	}

	void UiRenderer::EnsureSampler()
	{
		if (m_Sampler)
		{
			return;
		}
		// Trilinear and clamped: pictures are drawn smaller than they were rendered, which without mips shimmers,
		// and a clamp keeps a 9-slice (or a blur's edge tap) from bleeding the opposite edge in.
		SamplerDesc s{};
		s.MinFilter = ::Snowstorm::Filter::Linear;
		s.MagFilter = ::Snowstorm::Filter::Linear;
		s.MipmapMode = SamplerMipmapMode::Linear;
		s.AddressU = SamplerAddressMode::ClampToEdge;
		s.AddressV = SamplerAddressMode::ClampToEdge;
		s.AddressW = SamplerAddressMode::ClampToEdge;
		s.EnableAnisotropy = false;
		s.DebugName = "UiSampler";
		m_Sampler = Sampler::Create(s);
		SS_CORE_ASSERT(m_Sampler, "Failed to create UI sampler");
	}

	bool UiRenderer::EnsurePipelines(const PixelFormat swapFormat)
	{
		ShaderLibrary& shaders = Application::Get().GetServiceManager().GetService<ShaderLibrary>();
		const Ref<Shader> ui = shaders.Load("Engine/Shaders/Ui.vert.hlsl", "Engine/Shaders/Ui.frag.hlsl");
		const Ref<Shader> post = shaders.Load("Engine/Shaders/UiPost.vert.hlsl", "Engine/Shaders/UiPost.frag.hlsl");
		if (!ui || !post || !ui->IsReady() || !post->IsReady())
		{
			return false; // async compile; the UI skips a frame or two at startup
		}
		EnsureSampler();

		const auto graphics = [](const Ref<Shader>& shader, const PixelFormat format, const bool blend,
		                         const uint32_t pushSize, const char* name)
		{
			PipelineDesc p{};
			p.Type = PipelineType::Graphics;
			p.Shader = shader;
			p.ColorFormats = {format};
			p.DepthFormat = PixelFormat::Unknown;
			p.Raster.Cull = CullMode::None;
			p.DepthStencil.EnableDepthTest = false;
			p.DepthStencil.EnableDepthWrite = false;
			p.Blend.Attachments = {PipelineBlendAttachment{.EnableBlend = blend, .Mode = BlendMode::Premultiplied}};
			p.PushConstants = {{.Offset = 0, .Size = pushSize, .Stages = ShaderStage::AllGraphics}};
			p.DebugName = name;
			const Ref<Pipeline> pipeline = Pipeline::Create(p);
			SS_CORE_ASSERT(pipeline, "Failed to create a UI pipeline");
			return pipeline;
		};
		if (!m_UiPipeline)
		{
			m_UiPipeline = graphics(ui, kLayerFormat, true, sizeof(UiPushConstants), "UiPipeline");
		}
		if (!m_UiMaskPipeline)
		{
			m_UiMaskPipeline = graphics(ui, kLayerFormat, false, sizeof(UiPushConstants), "UiMaskPipeline");
		}
		for (const auto& [format, blend] : {std::pair{kLayerFormat, false}, std::pair{kLayerFormat, true}, std::pair{swapFormat, true}})
		{
			if (!m_PostPipelines.contains({format, blend}))
			{
				m_PostPipelines[{format, blend}] = graphics(post, format, blend, sizeof(PostPush), "UiPostPipeline");
			}
		}
		if (!m_PostSet)
		{
			const auto& layouts = m_PostPipelines.begin()->second->GetSetLayouts();
			SS_CORE_ASSERT(layouts.size() > kSetIndex && layouts[kSetIndex], "UI post pipeline missing set=1 layout");
			DescriptorSetDesc d{};
			d.DebugName = "UiPost_Set1";
			m_PostSet = DescriptorSet::Create(layouts[kSetIndex], d);
			m_PostSet->SetSampler(kSamplerBinding, m_Sampler);
			m_PostSet->Commit();
		}
		return true;
	}

	Ref<Pipeline> UiRenderer::PostPipeline(const PixelFormat format, const bool blend) const
	{
		const auto it = m_PostPipelines.find({format, blend});
		return it == m_PostPipelines.end() ? nullptr : it->second;
	}

	UiRenderer::FrameResources& UiRenderer::EnsureFrameResources(const uint32_t frameIndex)
	{
		const uint32_t frames = Renderer::GetFramesInFlight();
		if (m_Frames.size() < frames)
		{
			m_Frames.resize(frames);
		}
		FrameResources& fr = m_Frames[frameIndex];
		if (!fr.Set)
		{
			const auto& layouts = m_UiPipeline->GetSetLayouts();
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
		const size_t vertexBytes = std::max<size_t>(m_FrameVertices.size() * sizeof(uint32_t), 4);
		const size_t indexBytes = std::max<size_t>(m_FrameIndices.size() * sizeof(uint32_t), 4);
		const size_t gradientBytes = std::max<size_t>(m_FrameGradients.size(), 1) * sizeof(GradientGpu);
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
		if (fr.GradientCapacity < gradientBytes)
		{
			fr.GradientCapacity = grow(fr.GradientCapacity, gradientBytes);
			fr.Gradients = Buffer::Create(fr.GradientCapacity, BufferUsage::Storage, nullptr, true, "UiGradients");
		}
		return fr;
	}

	// ---- replay

	void UiRenderer::AddPasses(RenderGraph& graph, const uint32_t frameIndex, const PixelFormat swapFormat)
	{
		m_FrameReady = false;
		if (m_Width == 0 || m_Height == 0 || !EnsurePipelines(swapFormat))
		{
			return;
		}

		FrameResources& fr = EnsureFrameResources(frameIndex);
		if (!m_FrameVertices.empty())
		{
			fr.Vertices->SetData(m_FrameVertices.data(), m_FrameVertices.size() * sizeof(uint32_t), 0);
			fr.Indices->SetData(m_FrameIndices.data(), m_FrameIndices.size() * sizeof(uint32_t), 0);
		}
		if (!m_FrameGradients.empty())
		{
			fr.Gradients->SetData(m_FrameGradients.data(), m_FrameGradients.size() * sizeof(GradientGpu), 0);
		}
		fr.Set->SetBuffer(kVerticesBinding, {.Buffer = fr.Vertices, .Offset = 0, .Range = 0});
		fr.Set->SetSampler(kSamplerBinding, m_Sampler);
		fr.Set->SetBuffer(kGradientsBinding, {.Buffer = fr.Gradients, .Offset = 0, .Range = 0});
		fr.Set->Commit();

		// SS_UI_TRACE=1 logs the recording of each frame that saves a layer (a box-shadow bake), the first 40.
		static const bool trace = std::getenv("SS_UI_TRACE") != nullptr;
		static int traced = 0;
		const bool saves = std::any_of(m_Ops.begin(), m_Ops.end(), [](const Op& o) { return std::holds_alternative<SaveOp>(o); });
		if (trace && saves && traced < 40)
		{
			++traced;
			SS_CORE_INFO("UI trace: frame {} {} ops, {}x{}", m_FrameCounter, m_Ops.size(), m_Width, m_Height);
			for (const Op& op : m_Ops)
			{
				if (const auto* g = std::get_if<SegmentOp>(&op))
				{
					SS_CORE_INFO("  segment layer {} clear {} draws {}", g->Layer, g->Clear, g->Draws.size());
					for (const DrawCommand& d : g->Draws)
					{
						SS_CORE_INFO("    draw idx {} tex {} ({}) grad {} mask {} tr ({:.0f},{:.0f}) scissor {} [{} {} {} {}]", d.IndexCount,
						             d.TextureIndex, d.Textured, d.Gradient, d.Mask, d.Translation.x, d.Translation.y, d.Scissor,
						             d.ScissorRect.Left(), d.ScissorRect.Top(), d.ScissorRect.Width(), d.ScissorRect.Height());
					}
				}
				else if (const auto* m = std::get_if<MaskOp>(&op))
				{
					SS_CORE_INFO("  mask op {} into {} prev {} idx {}", static_cast<int>(m->Operation), m->Into, m->Previous, m->Draw.IndexCount);
				}
				else if (const auto* c = std::get_if<CompositeOp>(&op))
				{
					SS_CORE_INFO("  composite {} -> {} blend {} filters {} mask {}", c->Source, c->Destination,
					             static_cast<int>(c->Blend), c->Filters.size(), c->Mask);
				}
				else if (const auto* sv = std::get_if<SaveOp>(&op))
				{
					SS_CORE_INFO("  save layer {} region [{} {} {} {}] -> tex {}", sv->Layer, sv->Region.Left(), sv->Region.Top(),
					             sv->Region.Width(), sv->Region.Height(), sv->Into->Bindless);
				}
			}
		}

		std::vector<Ref<Texture>> saved; // written this frame, so later passes must declare them to sample them
		for (const Op& op : m_Ops)
		{
			if (const auto* segment = std::get_if<SegmentOp>(&op))
			{
				AddSegmentPass(graph, fr, *segment, saved);
			}
			else if (const auto* mask = std::get_if<MaskOp>(&op))
			{
				AddMaskPasses(graph, fr, *mask);
			}
			else if (const auto* composite = std::get_if<CompositeOp>(&op))
			{
				AddCompositePasses(graph, *composite);
			}
			else if (const auto* save = std::get_if<SaveOp>(&op))
			{
				const Target& layer = m_Layers[save->Layer];
				PostPush p;
				p.Mode = kPostCopy;
				p.Texture = layer.Bindless;
				p.SampleOffset[0] = static_cast<float>(save->Region.Left()) / static_cast<float>(layer.Width);
				p.SampleOffset[1] = static_cast<float>(save->Region.Top()) / static_cast<float>(layer.Height);
				p.SampleScale[0] = static_cast<float>(save->Region.Width()) / static_cast<float>(layer.Width);
				p.SampleScale[1] = static_cast<float>(save->Region.Height()) / static_cast<float>(layer.Height);
				AddPostPass(graph, "UI save layer", *save->Into->Saved, true, false, p, {layer.Texture});
				saved.push_back(save->Into->Texture);
			}
		}
		m_SavedThisFrame = std::move(saved);
		m_FrameReady = true;
	}

	void UiRenderer::RecordDraw(CommandContext& ctx, const FrameResources& fr, const DrawCommand& draw, const uint32_t flags,
	                            const float maskValue) const
	{
		if (draw.Scissor)
		{
			const int x0 = std::clamp(draw.ScissorRect.Left(), 0, static_cast<int>(m_Width));
			const int y0 = std::clamp(draw.ScissorRect.Top(), 0, static_cast<int>(m_Height));
			const int x1 = std::clamp(draw.ScissorRect.Right(), 0, static_cast<int>(m_Width));
			const int y1 = std::clamp(draw.ScissorRect.Bottom(), 0, static_cast<int>(m_Height));
			if (x1 <= x0 || y1 <= y0)
			{
				return; // clipped away entirely
			}
			ctx.SetScissor(static_cast<uint32_t>(x0), static_cast<uint32_t>(y0), static_cast<uint32_t>(x1 - x0),
			               static_cast<uint32_t>(y1 - y0));
		}
		else
		{
			ctx.SetScissor(0, 0, m_Width, m_Height);
		}

		UiPushConstants push{};
		std::memcpy(push.Transform, draw.Transform.data(), sizeof(push.Transform));
		push.Translation[0] = draw.Translation.x;
		push.Translation[1] = draw.Translation.y;
		push.TextureIndex = draw.TextureIndex;
		push.Flags = flags | (draw.Textured ? kFlagTextured : 0u) | (draw.Gradient >= 0 ? kFlagGradient : 0u);
		push.GradientIndex = static_cast<uint32_t>(std::max(draw.Gradient, 0));
		push.MaskIndex = MaskBindless(draw.Mask);
		push.MaskValue = maskValue;
		push.UvScale[0] = draw.UvScale.x;
		push.UvScale[1] = draw.UvScale.y;
		ctx.PushConstants(&push, sizeof(push), 0);
		ctx.DrawIndexed(fr.Indices, draw.IndexCount, 1, draw.FirstIndex, 0, 0);
	}

	void UiRenderer::AddSegmentPass(RenderGraph& graph, const FrameResources& fr, const SegmentOp& op,
	                                std::vector<Ref<Texture>> reads)
	{
		const Target& layer = m_Layers[op.Layer];
		for (const DrawCommand& d : op.Draws)
		{
			if (d.Mask >= 0)
			{
				reads.push_back(m_Masks[static_cast<size_t>(d.Mask)].Texture);
			}
		}
		const FrameResources* frame = &fr;
		const SegmentOp* segment = &op; // m_Ops is stable until the next BeginFrame, after the graph has run
		graph.AddPass({.Name = "UI",
		               .Target = op.Clear ? layer.Clear : layer.Load,
		               .Reads = Sampled(reads),
		               .Execute = [this, frame, segment, w = layer.Width, h = layer.Height](CommandContext& c)
		               {
			               if (segment->Draws.empty())
			               {
				               return; // the pass exists for its clear
			               }
			               c.BindPipeline(m_UiPipeline);
			               c.BindDescriptorSet(frame->Set, kSetIndex);
			               c.BindGlobalResources();
			               c.SetViewport(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h));
			               for (const DrawCommand& d : segment->Draws)
			               {
				               RecordDraw(c, *frame, d, 0, 0.0f);
			               }
		               }});
	}

	void UiRenderer::AddMaskPasses(RenderGraph& graph, const FrameResources& fr, const MaskOp& op)
	{
		const FrameResources* frame = &fr;
		const auto drawMask = [this, frame, &graph](const Target& into, const bool startFull, const DrawCommand& draw,
		                                              const float value)
		{
			graph.AddPass({.Name = "UI clip mask",
			               .Target = startFull ? into.ClearOne : into.Clear,
			               .Execute = [this, frame, draw, value, w = into.Width, h = into.Height](CommandContext& c)
			               {
				               c.BindPipeline(m_UiMaskPipeline);
				               c.BindDescriptorSet(frame->Set, kSetIndex);
				               c.BindGlobalResources();
				               c.SetViewport(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h));
				               RecordDraw(c, *frame, draw, kFlagMaskWrite, value);
			               }});
		};
		const Target& into = m_Masks[static_cast<size_t>(op.Into)];
		switch (op.Operation)
		{
		case Rml::ClipMaskOperation::Set:
			drawMask(into, false, op.Draw, 1.0f);
			break;
		case Rml::ClipMaskOperation::SetInverse:
			drawMask(into, true, op.Draw, 0.0f);
			break;
		case Rml::ClipMaskOperation::Intersect:
		{
			const Target& scratch = m_Masks[static_cast<size_t>(op.Scratch)];
			const Target& previous = m_Masks[static_cast<size_t>(op.Previous)];
			drawMask(scratch, false, op.Draw, 1.0f);
			PostPush p;
			p.Mode = kPostMaskMultiply;
			p.Texture = previous.Bindless;
			p.Texture2 = scratch.Bindless;
			AddPostPass(graph, "UI clip mask intersect", into, true, false, p, {previous.Texture, scratch.Texture});
			break;
		}
		}
	}

	UiRenderer::Cursor UiRenderer::AddBlur(RenderGraph& graph, const Cursor in, const float sigma, const int keep)
	{
		// Downsample until sigma is a handful of taps at this resolution: each level halves both the cost and
		// the taps, and a Gaussian that wide has no detail a lower resolution would lose.
		Cursor c = in;
		while (sigma / static_cast<float>(1u << c.Level) > kMaxSigmaTaps && c.Level + 1 < kLevels &&
		       (m_TargetWidth >> (c.Level + 1)) >= 8 && (m_TargetHeight >> (c.Level + 1)) >= 8)
		{
			const uint32_t next = c.Level + 1;
			const Target& t = Scratch(next, 0);
			PostPush p;
			p.Mode = kPostCopy;
			p.Texture = c.Image->Bindless;
			AddPostPass(graph, "UI blur downsample", t, true, false, p, {c.Image->Texture});
			c = {&t, next, 0};
		}
		const float taps = sigma / static_cast<float>(1u << c.Level);
		const int keepHere = c.Level == in.Level ? keep : -1;

		const int hs = FreeSlot(c.Slot, keepHere);
		const Target& h = Scratch(c.Level, hs);
		PostPush ph;
		ph.Mode = kPostBlur;
		ph.Sigma = taps;
		ph.TexelStep[0] = 1.0f / static_cast<float>(h.Width);
		ph.Texture = c.Image->Bindless;
		AddPostPass(graph, "UI blur", h, true, false, ph, {c.Image->Texture});

		const int vs = FreeSlot(hs, keepHere);
		const Target& v = Scratch(c.Level, vs);
		PostPush pv;
		pv.Mode = kPostBlur;
		pv.Sigma = taps;
		pv.TexelStep[1] = 1.0f / static_cast<float>(v.Height);
		pv.Texture = h.Bindless;
		AddPostPass(graph, "UI blur", v, true, false, pv, {h.Texture});
		return {&v, c.Level, vs};
	}

	void UiRenderer::AddCompositePasses(RenderGraph& graph, const CompositeOp& op)
	{
		Cursor cur{&m_Layers[op.Source], 0, -1};
		for (const Filter& filter : op.Filters)
		{
			const Filter* f = &filter;
			switch (f->Type)
			{
			case Filter::Kind::Opacity:
			case Filter::Kind::ColorMatrix:
			{
				const int s = FreeSlot(cur.Slot, -1);
				const Target& t = Scratch(cur.Level, s);
				PostPush p;
				p.Texture = cur.Image->Bindless;
				if (f->Type == Filter::Kind::Opacity)
				{
					p.Mode = kPostOpacity;
					p.Colour[3] = f->Value;
				}
				else
				{
					p.Mode = kPostColorMatrix;
					std::memcpy(p.Matrix, f->Matrix.data(), sizeof(p.Matrix));
				}
				AddPostPass(graph, "UI filter", t, true, false, p, {cur.Image->Texture});
				cur = {&t, cur.Level, s};
				break;
			}
			case Filter::Kind::Blur:
				if (f->Sigma > 0.25f)
				{
					cur = AddBlur(graph, cur, f->Sigma, -1);
				}
				break;
			case Filter::Kind::DropShadow:
			{
				const Cursor original = cur;
				const int s = FreeSlot(original.Slot, -1);
				const Target& shadow = Scratch(original.Level, s);
				PostPush p;
				p.Mode = kPostShadow;
				std::memcpy(p.Colour, f->Colour.data(), sizeof(p.Colour));
				p.ShadowOffset[0] = f->Offset[0] / static_cast<float>(m_TargetWidth);
				p.ShadowOffset[1] = f->Offset[1] / static_cast<float>(m_TargetHeight);
				p.Texture = original.Image->Bindless;
				AddPostPass(graph, "UI drop-shadow", shadow, true, false, p, {original.Image->Texture});

				Cursor blurred{&shadow, original.Level, s};
				if (f->Sigma > 0.25f)
				{
					blurred = AddBlur(graph, blurred, f->Sigma, original.Slot);
				}
				const int o = FreeSlot(original.Slot, blurred.Level == original.Level ? blurred.Slot : -1);
				const Target& out = Scratch(original.Level, o);
				PostPush q;
				q.Mode = kPostOver;
				q.Texture = blurred.Image->Bindless;
				q.Texture2 = original.Image->Bindless;
				AddPostPass(graph, "UI drop-shadow under", out, true, false, q, {blurred.Image->Texture, original.Image->Texture});
				cur = {&out, original.Level, o};
				break;
			}
			}
		}

		const Target& destination = m_Layers[op.Destination];
		if (cur.Image == &destination)
		{
			return; // a layer onto itself, unfiltered: nothing moves
		}
		PostPush p;
		p.Mode = kPostCopy;
		p.Texture = cur.Image->Bindless;
		p.Mask = MaskBindless(op.Mask);
		std::vector<Ref<Texture>> reads{cur.Image->Texture};
		if (op.Mask >= 0)
		{
			reads.push_back(m_Masks[static_cast<size_t>(op.Mask)].Texture);
		}
		AddPostPass(graph, "UI composite", destination, false, op.Blend == Rml::BlendMode::Blend, p, std::move(reads),
		            op.Scissor, op.ScissorRect);
	}

	void UiRenderer::AddPostPass(RenderGraph& graph, const char* name, const Target& into, const bool clear, const bool blend,
	                             const PostPush& push, std::vector<Ref<Texture>> reads, const bool scissor,
	                             const Rml::Rectanglei scissorRect)
	{
		graph.AddPass({.Name = name,
		               .Target = clear ? into.Clear : into.Load,
		               .Reads = Sampled(reads),
		               .Execute = [this, push, blend, scissor, scissorRect, w = into.Width, h = into.Height](CommandContext& c)
		               {
			               c.BindPipeline(PostPipeline(kLayerFormat, blend));
			               c.BindDescriptorSet(m_PostSet, kSetIndex);
			               c.BindGlobalResources();
			               c.SetViewport(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h));
			               if (scissor)
			               {
				               const int x0 = std::clamp(scissorRect.Left(), 0, static_cast<int>(w));
				               const int y0 = std::clamp(scissorRect.Top(), 0, static_cast<int>(h));
				               const int x1 = std::clamp(scissorRect.Right(), 0, static_cast<int>(w));
				               const int y1 = std::clamp(scissorRect.Bottom(), 0, static_cast<int>(h));
				               if (x1 <= x0 || y1 <= y0)
				               {
					               return;
				               }
				               c.SetScissor(static_cast<uint32_t>(x0), static_cast<uint32_t>(y0), static_cast<uint32_t>(x1 - x0),
				                            static_cast<uint32_t>(y1 - y0));
			               }
			               else
			               {
				               c.SetScissor(0, 0, w, h);
			               }
			               c.PushConstants(&push, sizeof(push), 0);
			               c.Draw(3);
		               }});
	}

	void UiRenderer::Composite(CommandContext& ctx, const PixelFormat format)
	{
		if (!m_FrameReady || m_Layers.empty())
		{
			return;
		}
		const Ref<Pipeline> pipeline = PostPipeline(format, true);
		if (!pipeline)
		{
			return;
		}
		ctx.BindPipeline(pipeline);
		ctx.BindDescriptorSet(m_PostSet, kSetIndex);
		ctx.BindGlobalResources();
		ctx.SetViewport(0.0f, 0.0f, static_cast<float>(m_Width), static_cast<float>(m_Height));
		ctx.SetScissor(0, 0, m_Width, m_Height);
		PostPush p;
		p.Mode = kPostCopy;
		p.Texture = m_Layers[0].Bindless;
		ctx.PushConstants(&p, sizeof(p), 0);
		ctx.Draw(3);
	}

	std::vector<Ref<Texture>> UiRenderer::CompositeReads() const
	{
		if (!m_FrameReady || m_Layers.empty())
		{
			return {};
		}
		std::vector<Ref<Texture>> reads{m_Layers[0].Texture};
		reads.insert(reads.end(), m_SavedThisFrame.begin(), m_SavedThisFrame.end());
		return reads;
	}
}
