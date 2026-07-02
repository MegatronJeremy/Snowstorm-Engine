#include "EditorTheme.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <imgui.h>

#include <array>
#include <cctype>
#include <filesystem>
#include <string>

namespace Snowstorm::EditorTheme
{
	namespace
	{
		// NERV / Evangelion palette: near-black bg, amber primary, warm-white readable text
		// (text is intentionally NOT the accent color so it stays crisp), red for warnings.
		constexpr ImVec4 Black{0.039f, 0.039f, 0.031f, 1.00f}; // window bg (#0a0a08)
		constexpr ImVec4 Panel{0.078f, 0.067f, 0.039f, 1.00f}; // child/frame bg (#14110a)
		constexpr ImVec4 PanelHover{0.16f, 0.12f, 0.05f, 1.00f};

		constexpr ImVec4 Green{1.00f, 0.40f, 0.00f, 1.00f};       // primary accent: amber (#ff6600)
		constexpr ImVec4 GreenDim{0.36f, 0.16f, 0.02f, 1.00f};    // dim amber fill
		constexpr ImVec4 GreenBright{1.00f, 0.65f, 0.19f, 1.00f}; // bright amber (#ffa530)

		// Medium amber for hover/active backgrounds that sit UNDER warm-white text. Bright amber
		// here makes the light text unreadable (light-on-light), so text-bearing widget states use
		// these dim/medium tones; bright amber is reserved for thin elements with no text on top.
		constexpr ImVec4 HoverBg{0.30f, 0.14f, 0.02f, 1.00f};  // dim amber, text stays readable
		constexpr ImVec4 ActiveBg{0.46f, 0.22f, 0.03f, 1.00f}; // medium amber, still readable

		constexpr ImVec4 Magenta{1.00f, 0.65f, 0.19f, 1.00f}; // secondary/active: bright amber
		constexpr ImVec4 MagentaDim{0.48f, 0.24f, 0.03f, 1.00f};

		constexpr ImVec4 Text{1.00f, 0.914f, 0.812f, 1.00f}; // warm white, readable (#ffe9cf)
		constexpr ImVec4 TextDim{0.60f, 0.50f, 0.38f, 1.00f};
		constexpr ImVec4 Border{0.48f, 0.227f, 0.03f, 1.00f}; // amber border (#7a3a08)
		constexpr ImVec4 Alert{1.00f, 0.10f, 0.05f, 1.00f};   // NERV red (#ff1a0d)
	}

	void ApplyEvangelion()
	{
		ImGuiStyle& style = ImGui::GetStyle();

		// ---- Shape: everything sharp, hard borders, dense instrument-panel spacing.
		style.WindowRounding = 0.0f;
		style.ChildRounding = 0.0f;
		style.FrameRounding = 0.0f;
		style.PopupRounding = 0.0f;
		style.ScrollbarRounding = 0.0f;
		style.GrabRounding = 0.0f;
		style.TabRounding = 0.0f;

		style.WindowBorderSize = 1.0f;
		style.ChildBorderSize = 1.0f;
		style.FrameBorderSize = 1.0f;
		style.PopupBorderSize = 1.0f;
		style.TabBorderSize = 1.0f;

		style.WindowPadding = ImVec2(6.0f, 6.0f);
		style.FramePadding = ImVec2(6.0f, 3.0f);
		style.ItemSpacing = ImVec2(6.0f, 4.0f);
		style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
		style.ScrollbarSize = 12.0f;
		style.GrabMinSize = 8.0f;
		style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
		style.CellPadding = ImVec2(4.0f, 3.0f); // breathing room between inspector rows

		// ---- Colors.
		ImVec4* c = style.Colors;
		c[ImGuiCol_Text] = Text;
		c[ImGuiCol_TextDisabled] = TextDim;
		c[ImGuiCol_WindowBg] = Black;
		c[ImGuiCol_ChildBg] = Black;
		c[ImGuiCol_PopupBg] = Panel;
		c[ImGuiCol_Border] = Border;
		c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

		c[ImGuiCol_FrameBg] = Panel;
		c[ImGuiCol_FrameBgHovered] = PanelHover;
		c[ImGuiCol_FrameBgActive] = MagentaDim;

		c[ImGuiCol_TitleBg] = Panel;
		c[ImGuiCol_TitleBgActive] = GreenDim;
		c[ImGuiCol_TitleBgCollapsed] = Black;
		c[ImGuiCol_MenuBarBg] = Panel;

		c[ImGuiCol_ScrollbarBg] = Black;
		c[ImGuiCol_ScrollbarGrab] = GreenDim;
		c[ImGuiCol_ScrollbarGrabHovered] = Green;
		c[ImGuiCol_ScrollbarGrabActive] = Magenta;

		c[ImGuiCol_CheckMark] = Magenta;
		c[ImGuiCol_SliderGrab] = Green;
		c[ImGuiCol_SliderGrabActive] = Magenta;

		c[ImGuiCol_Button] = GreenDim;
		c[ImGuiCol_ButtonHovered] = HoverBg;
		c[ImGuiCol_ButtonActive] = ActiveBg;

		// Header drives tree-node / selectable / collapsing-header backgrounds (text on top).
		c[ImGuiCol_Header] = GreenDim;
		c[ImGuiCol_HeaderHovered] = HoverBg;
		c[ImGuiCol_HeaderActive] = ActiveBg;

		c[ImGuiCol_Separator] = Border;
		c[ImGuiCol_SeparatorHovered] = Green;
		c[ImGuiCol_SeparatorActive] = Magenta;

		c[ImGuiCol_ResizeGrip] = GreenDim;
		c[ImGuiCol_ResizeGripHovered] = Green;
		c[ImGuiCol_ResizeGripActive] = Magenta;

		c[ImGuiCol_Tab] = Panel;
		c[ImGuiCol_TabHovered] = HoverBg;
		c[ImGuiCol_TabActive] = ActiveBg;
		c[ImGuiCol_TabUnfocused] = Black;
		c[ImGuiCol_TabUnfocusedActive] = Panel;

		c[ImGuiCol_PlotLines] = Green;
		c[ImGuiCol_PlotLinesHovered] = Magenta;
		c[ImGuiCol_PlotHistogram] = Green;
		c[ImGuiCol_PlotHistogramHovered] = Magenta;

		c[ImGuiCol_TextSelectedBg] = MagentaDim;
		c[ImGuiCol_DragDropTarget] = Alert;
		c[ImGuiCol_NavHighlight] = Magenta;
		c[ImGuiCol_DockingPreview] = GreenDim;
		c[ImGuiCol_DockingEmptyBg] = Black;

		// Keep platform windows opaque when multi-viewport is on.
		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}
	}

	void LoadMonospaceFont()
	{
		// Try a few common names so dropping any one of them into Assets/Fonts just works.
		static constexpr std::array candidates = {
		    "Assets/Fonts/ShareTechMono-Regular.ttf",
		    "Assets/Fonts/JetBrainsMono-Regular.ttf",
		    "Assets/Fonts/IBMPlexMono-Regular.ttf",
		    "Assets/Fonts/Hack-Regular.ttf",
		};

		for (const char* path : candidates)
		{
			if (std::filesystem::exists(path))
			{
				ImGuiIO& io = ImGui::GetIO();
				if (io.Fonts->AddFontFromFileTTF(path, 16.0f) != nullptr)
				{
					SS_CORE_INFO("Editor theme: loaded monospace font '{}'.", path);
					return;
				}
				SS_CORE_WARN("Editor theme: failed to load font '{}'.", path);
			}
		}

		SS_CORE_INFO("Editor theme: no monospace font in Assets/Fonts; using built-in font.");
	}

	void SectionHeader(const char* label)
	{
		std::string upper(label);
		for (char& ch : upper)
		{
			ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
		}

		ImGui::PushStyleColor(ImGuiCol_Text, Green);
		ImGui::TextUnformatted(("// " + upper).c_str());
		ImGui::PopStyleColor();
		ImGui::Separator();
	}

	bool PrimaryButton(const char* label, const float width)
	{
		std::string upper(label);
		for (char& ch : upper)
		{
			ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
		}

		// Bright-amber fill with near-black text — the one high-emphasis action per panel. Bright amber
		// under text is normally unreadable (see the theme notes), so we force dark text on top here.
		ImGui::PushStyleColor(ImGuiCol_Button, Green);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GreenBright);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, GreenBright);
		ImGui::PushStyleColor(ImGuiCol_Text, Black);
		const float w = (width > 0.0f) ? width : ImGui::GetContentRegionAvail().x;
		const bool clicked = ImGui::Button(upper.c_str(), ImVec2(w, 0.0f));
		ImGui::PopStyleColor(4);
		return clicked;
	}

	unsigned int AccentColor()
	{
		return ImGui::GetColorU32(Green);
	}

	void WarningBanner(const char* text)
	{
		constexpr ImVec4 AlertDim{0.22f, 0.02f, 0.02f, 1.00f}; // dim red bar bg
		const ImVec2 size(ImGui::GetContentRegionAvail().x, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, AlertDim);
		ImGui::PushStyleColor(ImGuiCol_Border, Alert);
		ImGui::BeginChild("##warn", size, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
		ImGui::PushStyleColor(ImGuiCol_Text, Alert);
		ImGui::TextWrapped("\xE2\x96\xA0 %s", text); // U+25A0 BLACK SQUARE
		ImGui::PopStyleColor();
		ImGui::EndChild();
		ImGui::PopStyleColor(2);
	}
}
