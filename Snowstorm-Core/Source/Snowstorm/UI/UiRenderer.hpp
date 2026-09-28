#pragma once

#include "Snowstorm/Assets/TextureCache.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <RmlUi/Core/RenderInterface.h>

#include <array>
#include <future>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Snowstorm
{
	class CommandContext;

	// RmlUi's render interface on the engine's own resources: RmlUi describes the frame (geometry, textures,
	// scissor, transforms) while its context renders, and this turns that into one pipeline, one storage
	// buffer of vertices and one index buffer per frame, with everything per draw in a push constant.
	//
	// Two phases, both on the main thread. BeginFrame + RmlUi's Context::Render collect the frame on the CPU:
	// every draw copies its geometry into the frame's arrays, so nothing RmlUi releases afterwards can be read
	// by a draw that was already recorded. Draw, inside the render graph's pass, uploads the arrays and records.
	//
	// Images load off the main thread. LoadTexture answers RmlUi's layout at once from the file header, and
	// decodes on the JobSystem; a draw that uses an image still decoding is skipped, and RmlUi re-issues it
	// every frame, so the image simply appears when it is ready. A 1024x1024 PNG with its mip chain is tens of
	// milliseconds, and a UI that shows a new picture per click cannot pay that on the frame.
	//
	// Colour: RmlUi 6 hands over premultiplied alpha everywhere (vertex colours, generated textures), so images
	// are premultiplied at decode and the pipeline blends One / OneMinusSrcAlpha. Textures are UNORM and the
	// target is the UNORM swapchain, so CSS colours land as authored.
	//
	// Not implemented yet (RmlUi then skips the effect): clip masks, layers, filters (blur, drop-shadow,
	// box-shadow) and shaders (gradients).
	class UiRenderer final : public Rml::RenderInterface
	{
	public:
		UiRenderer();
		~UiRenderer() override;

		// ---- Rml::RenderInterface
		Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
		void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
		void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
		Rml::TextureHandle LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) override;
		Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions) override;
		void ReleaseTexture(Rml::TextureHandle texture) override;
		void EnableScissorRegion(bool enable) override;
		void SetScissorRegion(Rml::Rectanglei region) override;
		void SetTransform(const Rml::Matrix4f* transform) override;

		// ---- the frame (main thread)
		// Start collecting a frame for a target of this size. Also uploads images whose decode finished.
		void BeginFrame(uint32_t width, uint32_t height);
		// Upload and record the collected frame into `ctx`, whose render pass targets `colorFormat`.
		void Draw(CommandContext& ctx, uint32_t frameIndex, PixelFormat colorFormat);

		// Images still decoding, for a caller that wants to know the UI is not finished drawing.
		[[nodiscard]] size_t PendingTextures() const;

	private:
		struct Geometry
		{
			std::vector<Rml::Vertex> Vertices;
			std::vector<int> Indices;
		};

		struct TextureEntry
		{
			Ref<Texture> Texture;
			uint32_t Bindless = 0;
			std::string Source; // for the log line when a decode fails
			std::optional<std::future<std::optional<CookedTexture>>> Decoding;
			bool Failed = false;
		};

		struct DrawCommand
		{
			uint32_t FirstIndex = 0;
			uint32_t IndexCount = 0;
			Rml::Vector2f Translation;
			uint32_t TextureIndex = 0;
			bool Textured = false;
			bool Scissor = false;
			Rml::Rectanglei ScissorRect;
			std::array<float, 16> Transform{}; // projection * element transform, column-major
		};

		struct FrameResources
		{
			Ref<DescriptorSet> Set;
			Ref<Buffer> Vertices;
			Ref<Buffer> Indices;
			size_t VertexCapacity = 0; // bytes
			size_t IndexCapacity = 0;  // bytes
		};

		void EnsurePipeline(PixelFormat colorFormat);
		void EnsureSampler();
		FrameResources& EnsureFrameResources(uint32_t frameIndex, size_t vertexBytes, size_t indexBytes);
		void PumpDecodes();
		void UpdateTransform();

		Ref<Pipeline> m_Pipeline;
		PixelFormat m_ColorFormat = PixelFormat::Unknown;
		Ref<Sampler> m_Sampler;
		std::vector<FrameResources> m_Frames;
		std::unordered_set<TextureEntry*> m_Textures; // every live handle, so decodes can be pumped

		// The frame being collected.
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		std::vector<uint32_t> m_FrameVertices; // five words per Rml::Vertex, see Ui.hlsli
		std::vector<uint32_t> m_FrameIndices;  // rebased onto m_FrameVertices
		std::vector<DrawCommand> m_Commands;
		std::unordered_map<const Geometry*, std::pair<uint32_t, uint32_t>> m_Uploaded; // geometry -> first index, count
		bool m_ScissorEnabled = false;
		Rml::Rectanglei m_ScissorRect;
		bool m_HasTransform = false;
		std::array<float, 16> m_ElementTransform{};
		std::array<float, 16> m_Combined{};
	};
}
