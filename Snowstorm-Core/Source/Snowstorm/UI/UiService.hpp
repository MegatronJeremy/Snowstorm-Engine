#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Events/EventBus.hpp"
#include "Snowstorm/Render/RenderEnums.hpp"
#include "Snowstorm/Service/Service.hpp"

#include <filesystem>
#include <vector>

namespace Rml
{
	class Context;
	class ElementDocument;
}

namespace Snowstorm
{
	class CommandContext;
	class UiRenderer;
	class UiSystemInterface;

	// The game UI layer: RmlUi (HTML/CSS-style documents, data bindings, animations) drawn by the engine's
	// own UI pass, under ImGui, which stays the debug overlay on top.
	//
	// Opt-in. A game registers the service and asks for Context(); nothing is initialised, no event is
	// consumed and nothing is drawn until then, so a game or tool that never opens a document pays nothing.
	//
	// Frame order: the game updates its documents and data models during its own update; RenderSystem then
	// calls BuildFrame (RmlUi update, layout, and the frame's geometry collected on the CPU) before it records
	// the swapchain pass, and Draw from inside that pass. Input arrives through the EventBus and is consumed
	// only while the pointer is over an element, unless ImGui has it.
	class UiService final : public Service
	{
	public:
		UiService();
		~UiService() override;

		// The one context, sized to the window, created on first use.
		Rml::Context* Context();
		[[nodiscard]] bool IsActive() const { return m_Context != nullptr; }

		// A font face, loaded once for the life of the UI; `fallback` makes it the face of last resort for
		// glyphs no other face has. False if the file is missing or unusable.
		bool LoadFontFace(const std::filesystem::path& path, bool fallback = false);
		// Load (not show) a document; null on failure, with RmlUi's reason in the log.
		Rml::ElementDocument* LoadDocument(const std::filesystem::path& path);

		// RmlUi's `dp` unit in pixels. Documents authored in dp scale with this; 1 is RmlUi's default.
		void SetDpRatio(float ratio);

		// Whether the pointer is over, or dragging, an element: a caller mixing the UI with other input (the
		// debug overlay, a world view) asks this before acting on a click.
		[[nodiscard]] bool WantsMouse() const;
		// Images still decoding off the main thread.
		[[nodiscard]] size_t PendingTextures() const;

		// ---- called by RenderSystem
		void BuildFrame(uint32_t width, uint32_t height);
		void Draw(CommandContext& ctx, uint32_t frameIndex, PixelFormat colorFormat);

		// Release every document, texture and font while the renderer is still up. The destructor calls it too,
		// but services are destroyed in no promised order, so a game calls this from its own shutdown.
		void Shutdown();

	private:
		void Initialise();
		void Subscribe();

		Scope<UiRenderer> m_Renderer;
		Scope<UiSystemInterface> m_System;
		Rml::Context* m_Context = nullptr;
		bool m_Initialised = false;
		int m_Modifiers = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		std::vector<EventBus::Connection> m_Connections;
	};
}
