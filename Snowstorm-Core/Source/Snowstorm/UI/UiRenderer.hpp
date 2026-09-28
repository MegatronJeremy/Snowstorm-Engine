#pragma once

#include "Snowstorm/Assets/TextureCache.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/RenderGraph.hpp"
#include "Snowstorm/Render/RenderTarget.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <RmlUi/Core/RenderInterface.h>

#include <array>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace Snowstorm
{
	class CommandContext;

	// RmlUi's render interface on the engine's own resources, both the basic and the advanced half.
	//
	// RECORD, THEN REPLAY. While RmlUi's context renders (BeginFrame, then Context::Render, on the main thread)
	// nothing touches the GPU: every call is recorded, in order, as one of four operations. A Segment is a run of
	// draws into one layer; a Composite takes a layer through its filters and onto another; a Save copies a
	// layer's scissored region into a texture RmlUi keeps (how a box-shadow is baked once and then drawn like any
	// image); a Mask changes the clip mask. AddPasses then uploads the frame's geometry once and turns the
	// operations, in the same order, into render-graph passes, one per change of target, each declaring what it
	// samples so the graph places the barriers. Draws copy their geometry into the frame's arrays as they are
	// recorded, so nothing RmlUi releases afterwards can be read by a draw already recorded.
	//
	// LAYERS. The UI draws into its own base layer (layer 0) and pushed layers above it, all window-sized RGBA8
	// targets reused across frames; Composite draws the finished base layer into the swapchain from the pass that
	// composes it, under ImGui.
	//
	// FILTERS: opacity; brightness, contrast, grayscale, sepia, hue-rotate, saturate and invert as one colour
	// matrix each (the CSS Filter Effects formulas); blur, two passes of a Gaussian at a resolution chosen so
	// sigma stays under 8 taps; drop-shadow, the source's alpha tinted, moved, blurred and put under it.
	// SHADERS: linear-, radial- and conic-gradient, repeating or not, evaluated per pixel in the UI pass.
	// CLIP MASKS: window-sized coverage images rather than a stencil, read by every draw at its own pixel, so a
	// mask's edge keeps the antialiasing of the geometry that drew it. RmlUi uses them for overflow under a
	// transform or a border-radius, and to keep a box-shadow from showing through its own element.
	//
	// Images load off the main thread: LoadTexture answers RmlUi's layout at once from the file header and decodes
	// on the JobSystem, and a draw whose image is still decoding is skipped until it arrives.
	//
	// Colour: premultiplied everywhere, as RmlUi 6 composes; UNORM layers and a UNORM swapchain, so CSS colours
	// blend in the space they are authored in, as a browser does. Not implemented: mask-image.
	class UiRenderer final : public Rml::RenderInterface
	{
	public:
		UiRenderer();
		~UiRenderer() override;

		// ---- Rml::RenderInterface, basic
		Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
		void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
		void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
		Rml::TextureHandle LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) override;
		Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions) override;
		void ReleaseTexture(Rml::TextureHandle texture) override;
		void EnableScissorRegion(bool enable) override;
		void SetScissorRegion(Rml::Rectanglei region) override;

		// ---- Rml::RenderInterface, advanced
		void EnableClipMask(bool enable) override;
		void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) override;
		void SetTransform(const Rml::Matrix4f* transform) override;
		Rml::LayerHandle PushLayer() override;
		void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blendMode,
		                     Rml::Span<const Rml::CompiledFilterHandle> filters) override;
		void PopLayer() override;
		Rml::TextureHandle SaveLayerAsTexture() override;
		Rml::CompiledFilterHandle CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) override;
		void ReleaseFilter(Rml::CompiledFilterHandle filter) override;
		Rml::CompiledShaderHandle CompileShader(const Rml::String& name, const Rml::Dictionary& parameters) override;
		void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
		                  Rml::TextureHandle texture) override;
		void ReleaseShader(Rml::CompiledShaderHandle shader) override;

		// ---- the frame (main thread)
		// Whether the UI's pipelines exist, creating them once their shaders have compiled. Until then nothing may
		// be recorded: RmlUi bakes some textures once and caches them (a box-shadow), and a bake recorded in a
		// frame whose passes cannot run would stay empty for good.
		bool Ready(PixelFormat swapFormat);
		// Start recording a frame for a target of this size. Also uploads images whose decode finished.
		void BeginFrame(uint32_t width, uint32_t height);
		// After RmlUi has rendered into the recording: upload it and add its passes to `graph`, in order.
		// `swapFormat` is the format Composite will draw into, so its pipeline is ready before the graph runs.
		void AddPasses(RenderGraph& graph, uint32_t frameIndex, PixelFormat swapFormat);
		// From inside the pass that composes `format`: the finished base layer, blended over what is there.
		void Composite(CommandContext& ctx, PixelFormat format);
		// What Composite samples, for that pass's Reads: the base layer, and every texture a Save wrote this frame
		// (so each leaves the frame in the sampled layout its later uses expect).
		[[nodiscard]] std::vector<Ref<Texture>> CompositeReads() const;

		// Images still decoding, for a caller that wants to know the UI is not finished drawing.
		[[nodiscard]] size_t PendingTextures() const;
		// Whether a live baked texture (a box-shadow) came out clipped to the window: RmlUi bakes into a
		// window-sized layer and keeps the result until the element changes, so a bake made while the window was
		// smaller stays cut short after it grows. The caller re-bakes (Rml::ReleaseTextures) when there is room.
		[[nodiscard]] bool HasClippedBakes() const { return m_ClippedBakes > 0; }

	private:
		struct Geometry
		{
			std::vector<Rml::Vertex> Vertices;
			std::vector<int> Indices;
			Rml::Vector2f Size; // of the vertices' bounding box
		};

		// A colour target the UI renders into and samples from: a layer, a mask, a filter's scratch, or a saved
		// layer region.
		struct Target
		{
			Ref<Texture> Texture;
			Ref<RenderTarget> Clear;    // the same view, cleared to transparent black on begin
			Ref<RenderTarget> ClearOne; // cleared to white: an inverse clip mask starts full
			Ref<RenderTarget> Load;     // and kept
			uint32_t Bindless = 0;
			uint32_t Width = 0;
			uint32_t Height = 0;
		};

		struct TextureEntry
		{
			Ref<Texture> Texture;
			uint32_t Bindless = 0;
			std::string Source; // for the log line when a decode fails
			std::optional<std::future<std::optional<CookedTexture>>> Decoding;
			std::optional<Target> Saved; // a layer region RmlUi keeps (SaveLayerAsTexture)
			bool Clipped = false;        // a Saved region that reached the window's edge
			bool File = false;           // loaded from Source (LoadTexture), so it can be parked and handed back
			Rml::Vector2i Dimensions;
		};

		struct Filter
		{
			enum class Kind
			{
				Opacity,
				ColorMatrix,
				Blur,
				DropShadow
			};
			Kind Type = Kind::Opacity;
			float Value = 1.0f;             // opacity
			float Sigma = 0.0f;             // blur, drop-shadow; in pixels
			std::array<float, 4> Colour{};  // drop-shadow, premultiplied
			std::array<float, 2> Offset{};  // drop-shadow, in pixels
			std::array<float, 12> Matrix{}; // colour matrix, packed as PostPush::Matrix
		};

		// Mirrors UiPostPush in UiPost.hlsli field-for-field (128 bytes, the push-constant minimum).
		struct PostPush
		{
			float Matrix[12]{};  // colour matrix: [0..2] what r contributes to r'g'b', [3] r' translation; then g, b
			float Colour[4]{};   // shadow colour (premultiplied), or opacity in [3]
			float SampleOffset[2]{0.0f, 0.0f};
			float SampleScale[2]{1.0f, 1.0f};
			float ShadowOffset[2]{};
			float TexelStep[2]{};
			float Sigma = 0.0f;
			uint32_t Mode = 0;
			uint32_t Texture = 0;
			uint32_t Texture2 = 0;
			uint32_t Mask = 0;
			uint32_t Pad[3]{};
		};

		// Mirrors UiGradient in Ui.hlsli field-for-field (352 bytes).
		struct GradientGpu
		{
			uint32_t Kind = 0;
			uint32_t Repeating = 0;
			uint32_t StopCount = 0;
			float Angle = 0.0f;
			float P0[2]{};
			float P1[2]{};
			float Colors[16][4]{};
			float Positions[16]{};
		};

		struct DrawCommand
		{
			uint32_t FirstIndex = 0;
			uint32_t IndexCount = 0;
			Rml::Vector2f Translation;
			uint32_t TextureIndex = 0;
			bool Textured = false;
			int Gradient = -1; // index into the frame's gradients
			int Mask = -1;     // clip mask slot in force, -1 for none
			bool Scissor = false;
			Rml::Rectanglei ScissorRect;
			std::array<float, 16> Transform{}; // projection * element transform, column-major
			Rml::Vector2f UvScale{1.0f, 1.0f}; // see UiPush.UvScale
		};

		struct SegmentOp
		{
			uint32_t Layer = 0;
			bool Clear = false;
			std::vector<DrawCommand> Draws;
		};
		struct CompositeOp
		{
			uint32_t Source = 0;
			uint32_t Destination = 0;
			Rml::BlendMode Blend = Rml::BlendMode::Blend;
			std::vector<Filter> Filters; // copies: RmlUi releases a filter as soon as it has composited with it
			int Mask = -1;
			bool Scissor = false;
			Rml::Rectanglei ScissorRect;
		};
		struct SaveOp
		{
			uint32_t Layer = 0;
			Rml::Rectanglei Region;
			TextureEntry* Into = nullptr;
		};
		struct MaskOp
		{
			Rml::ClipMaskOperation Operation = Rml::ClipMaskOperation::Set;
			DrawCommand Draw; // the geometry that shapes the mask
			int Into = 0;     // the mask slot written
			int Previous = -1; // Intersect: the mask it narrows
			int Scratch = -1;  // Intersect: where the geometry is drawn first
		};
		using Op = std::variant<SegmentOp, CompositeOp, SaveOp, MaskOp>;

		struct FrameResources
		{
			Ref<DescriptorSet> Set;
			Ref<Buffer> Vertices;
			Ref<Buffer> Indices;
			Ref<Buffer> Gradients;
			size_t VertexCapacity = 0;   // bytes
			size_t IndexCapacity = 0;    // bytes
			size_t GradientCapacity = 0; // bytes
		};

		// A position in a filter chain: which target holds the image, at which downsample level, in which scratch
		// slot (-1 when it is a layer, not scratch).
		struct Cursor
		{
			const Target* Image = nullptr;
			uint32_t Level = 0;
			int Slot = -1;
		};

		// ---- recording
		DrawCommand MakeDraw(const Geometry* g, Rml::Vector2f translation);
		void Record(const Geometry* g, Rml::Vector2f translation, Rml::TextureHandle texture, int gradient);
		SegmentOp& CurrentSegment();
		[[nodiscard]] uint32_t TopLayer() const { return m_LayerStack.back(); }
		void UpdateTransform();
		void PumpDecodes();

		// ---- resources
		bool EnsurePipelines(PixelFormat swapFormat);
		Ref<Pipeline> PostPipeline(PixelFormat format, bool blend) const;
		void EnsureSampler();
		FrameResources& EnsureFrameResources(uint32_t frameIndex);
		void EnsureTargets(uint32_t width, uint32_t height);
		static Target MakeTarget(uint32_t width, uint32_t height, const char* name);
		Target& Layer(uint32_t index);
		Target& Scratch(uint32_t level, int slot);

		// ---- replay
		void AddSegmentPass(RenderGraph& graph, const FrameResources& fr, const SegmentOp& op, std::vector<Ref<Texture>> reads);
		void AddMaskPasses(RenderGraph& graph, const FrameResources& fr, const MaskOp& op);
		void AddCompositePasses(RenderGraph& graph, const CompositeOp& op);
		void AddPostPass(RenderGraph& graph, const char* name, const Target& into, bool clear, bool blend, const PostPush& push,
		                 std::vector<Ref<Texture>> reads, bool scissor = false, Rml::Rectanglei scissorRect = {});
		Cursor AddBlur(RenderGraph& graph, Cursor in, float sigma, int keep);
		void RecordDraw(CommandContext& ctx, const FrameResources& fr, const DrawCommand& draw, uint32_t flags,
		                float maskValue) const;
		[[nodiscard]] uint32_t MaskBindless(int slot) const;
		// Keep a released texture or target alive until the frames that may still sample it have finished:
		// dropping the last Ref destroys the image at once, and RmlUi releases a box-shadow's texture whenever
		// the element's size changes, often while the frame that drew it is still on the GPU.
		void Retire(const Ref<Texture>& texture);
		void Retire(const Target& target);

		Ref<Pipeline> m_UiPipeline;     // draws into a layer, premultiplied
		Ref<Pipeline> m_UiMaskPipeline; // draws a clip mask, unblended
		std::map<std::pair<PixelFormat, bool>, Ref<Pipeline>> m_PostPipelines;
		Ref<DescriptorSet> m_PostSet; // the post passes' sampler, the same every frame
		Ref<Sampler> m_Sampler;
		std::vector<FrameResources> m_Frames;
		std::unordered_set<TextureEntry*> m_Textures; // every live handle, so decodes can be pumped
		// Images RmlUi released lately, by source, with the frame they were released on. Rml::ReleaseTextures
		// drops every image to re-bake the box-shadows; RmlUi asks for the same images again on the next frame,
		// and they come back from here instead of blinking out for a decode.
		std::unordered_map<std::string, std::pair<uint64_t, TextureEntry*>> m_Parked;
		size_t m_ClippedBakes = 0;
		uint64_t m_FrameCounter = 0;
		std::vector<std::pair<uint64_t, std::shared_ptr<void>>> m_Retired; // (frame released, what it keeps alive)

		// Targets, window-sized, rebuilt with the window.
		uint32_t m_TargetWidth = 0;
		uint32_t m_TargetHeight = 0;
		std::vector<Target> m_Layers;                 // index = RmlUi's LayerHandle; 0 is the base
		std::array<Target, 3> m_Masks;                // clip masks, rotated so a change never overwrites the one read
		std::vector<std::array<Target, 3>> m_Scratch; // per downsample level: three, as a drop-shadow needs at once

		// The frame being recorded.
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		bool m_FrameReady = false;
		std::vector<uint32_t> m_FrameVertices; // five words per Rml::Vertex, see Ui.hlsli
		std::vector<uint32_t> m_FrameIndices;  // rebased onto m_FrameVertices
		std::vector<GradientGpu> m_FrameGradients;
		std::unordered_map<const GradientGpu*, int> m_GradientSlots; // compiled shader -> index this frame
		std::unordered_map<const Geometry*, std::pair<uint32_t, uint32_t>> m_Uploaded; // geometry -> first index, count
		std::vector<Op> m_Ops;
		std::vector<uint32_t> m_LayerStack;
		std::vector<Ref<Texture>> m_SavedThisFrame;
		bool m_ScissorEnabled = false;
		Rml::Rectanglei m_ScissorRect;
		bool m_ClipEnabled = false;
		int m_MaskSlot = -1; // the clip mask RmlUi last shaped, -1 before any
		bool m_HasTransform = false;
		std::array<float, 16> m_ElementTransform{};
		std::array<float, 16> m_Combined{};
	};
}
