#include "UiService.hpp"

#include "UiRenderer.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Core/Window.hpp"
#include "Snowstorm/Events/KeyEvent.hpp"
#include "Snowstorm/Events/MouseEvent.hpp"

#include <GLFW/glfw3.h>
#include <RmlUi/Core.h>
#include <imgui.h>

#include <chrono>

namespace Snowstorm
{
	// Time and logging for RmlUi. Everything else keeps RmlUi's defaults (C file IO, relative paths joined to
	// the document's own directory, which is what lets a stylesheet name its images by relative path).
	class UiSystemInterface final : public Rml::SystemInterface
	{
	public:
		double GetElapsedTime() override
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_Start).count();
		}

		bool LogMessage(const Rml::Log::Type type, const Rml::String& message) override
		{
			switch (type)
			{
			case Rml::Log::LT_ERROR:
			case Rml::Log::LT_ASSERT:
				SS_CORE_ERROR("RmlUi: {}", message);
				break;
			case Rml::Log::LT_WARNING:
				SS_CORE_WARN("RmlUi: {}", message);
				break;
			default:
				SS_CORE_INFO("RmlUi: {}", message);
				break;
			}
			return true;
		}

	private:
		std::chrono::steady_clock::time_point m_Start = std::chrono::steady_clock::now();
	};

	namespace
	{
		Rml::Input::KeyIdentifier ToRml(const int key)
		{
			using namespace Rml::Input;
			if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
			{
				return static_cast<KeyIdentifier>(KI_A + (key - GLFW_KEY_A));
			}
			if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
			{
				return static_cast<KeyIdentifier>(KI_0 + (key - GLFW_KEY_0));
			}
			if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12)
			{
				return static_cast<KeyIdentifier>(KI_F1 + (key - GLFW_KEY_F1));
			}
			if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9)
			{
				return static_cast<KeyIdentifier>(KI_NUMPAD0 + (key - GLFW_KEY_KP_0));
			}
			switch (key)
			{
			case GLFW_KEY_SPACE: return KI_SPACE;
			case GLFW_KEY_ESCAPE: return KI_ESCAPE;
			case GLFW_KEY_ENTER: return KI_RETURN;
			case GLFW_KEY_KP_ENTER: return KI_NUMPADENTER;
			case GLFW_KEY_TAB: return KI_TAB;
			case GLFW_KEY_BACKSPACE: return KI_BACK;
			case GLFW_KEY_DELETE: return KI_DELETE;
			case GLFW_KEY_INSERT: return KI_INSERT;
			case GLFW_KEY_HOME: return KI_HOME;
			case GLFW_KEY_END: return KI_END;
			case GLFW_KEY_PAGE_UP: return KI_PRIOR;
			case GLFW_KEY_PAGE_DOWN: return KI_NEXT;
			case GLFW_KEY_LEFT: return KI_LEFT;
			case GLFW_KEY_RIGHT: return KI_RIGHT;
			case GLFW_KEY_UP: return KI_UP;
			case GLFW_KEY_DOWN: return KI_DOWN;
			case GLFW_KEY_LEFT_SHIFT: return KI_LSHIFT;
			case GLFW_KEY_RIGHT_SHIFT: return KI_RSHIFT;
			case GLFW_KEY_LEFT_CONTROL: return KI_LCONTROL;
			case GLFW_KEY_RIGHT_CONTROL: return KI_RCONTROL;
			case GLFW_KEY_LEFT_ALT: return KI_LMENU;
			case GLFW_KEY_RIGHT_ALT: return KI_RMENU;
			default: return KI_UNKNOWN;
			}
		}

		int ModifierOf(const int key)
		{
			switch (key)
			{
			case GLFW_KEY_LEFT_SHIFT:
			case GLFW_KEY_RIGHT_SHIFT: return Rml::Input::KM_SHIFT;
			case GLFW_KEY_LEFT_CONTROL:
			case GLFW_KEY_RIGHT_CONTROL: return Rml::Input::KM_CTRL;
			case GLFW_KEY_LEFT_ALT:
			case GLFW_KEY_RIGHT_ALT: return Rml::Input::KM_ALT;
			default: return 0;
			}
		}

		// The debug overlay owns the pointer while it is over one of its windows.
		bool ImGuiHasMouse()
		{
			return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
		}

		bool ImGuiHasKeyboard()
		{
			return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;
		}

		// RmlUi takes UTF-8 in a std::string; C++20's u8string is a different type.
		Rml::String Utf8(const std::filesystem::path& path)
		{
			const std::u8string s = path.u8string();
			return {s.begin(), s.end()};
		}
	}

	UiService::UiService() = default;

	UiService::~UiService()
	{
		Shutdown();
	}

	void UiService::Initialise()
	{
		if (m_Initialised)
		{
			return;
		}
		m_Initialised = true;
		m_Renderer = CreateScope<UiRenderer>();
		m_System = CreateScope<UiSystemInterface>();
		Rml::SetSystemInterface(m_System.get());
		Rml::SetRenderInterface(m_Renderer.get());
		if (!Rml::Initialise())
		{
			SS_CORE_ERROR("UiService: RmlUi did not initialise");
			return;
		}
		const Window& window = Application::Get().GetWindow();
		m_Width = window.GetWidth();
		m_Height = window.GetHeight();
		m_Context = Rml::CreateContext("main", {static_cast<int>(m_Width), static_cast<int>(m_Height)});
		if (!m_Context)
		{
			SS_CORE_ERROR("UiService: could not create the RmlUi context");
			return;
		}
		Subscribe();
		SS_CORE_INFO("UiService: RmlUi {} up, {}x{}", Rml::GetVersion(), m_Width, m_Height);
	}

	void UiService::Subscribe()
	{
		EventBus& bus = Application::Get().GetEventBus();
		// A handler returns true to consume. The UI consumes only what lands on one of its elements, so a
		// click on empty space still reaches whatever sits behind it.
		m_Connections.push_back(bus.Subscribe<MouseMovedEvent>(
		    [this](const MouseMovedEvent& e)
		    {
			    if (!m_Context || ImGuiHasMouse())
			    {
				    return false;
			    }
			    return !m_Context->ProcessMouseMove(static_cast<int>(e.mouseX), static_cast<int>(e.mouseY), m_Modifiers);
		    },
		    100));
		m_Connections.push_back(bus.Subscribe<MouseButtonPressedEvent>(
		    [this](const MouseButtonPressedEvent& e)
		    {
			    if (!m_Context || ImGuiHasMouse())
			    {
				    return false;
			    }
			    return !m_Context->ProcessMouseButtonDown(e.m_Button, m_Modifiers);
		    },
		    100));
		m_Connections.push_back(bus.Subscribe<MouseButtonReleasedEvent>(
		    [this](const MouseButtonReleasedEvent& e)
		    {
			    if (!m_Context)
			    {
				    return false;
			    }
			    // Always delivered: a press the UI took must see its release, or a drag never ends.
			    return !m_Context->ProcessMouseButtonUp(e.m_Button, m_Modifiers);
		    },
		    100));
		m_Connections.push_back(bus.Subscribe<MouseScrolledEvent>(
		    [this](const MouseScrolledEvent& e)
		    {
			    if (!m_Context || ImGuiHasMouse())
			    {
				    return false;
			    }
			    // GLFW scrolls positive up; RmlUi's positive delta scrolls down.
			    return !m_Context->ProcessMouseWheel(Rml::Vector2f{-e.xOffset, -e.yOffset}, m_Modifiers);
		    },
		    100));
		m_Connections.push_back(bus.Subscribe<KeyPressedEvent>(
		    [this](const KeyPressedEvent& e)
		    {
			    m_Modifiers |= ModifierOf(e.m_KeyCode);
			    if (!m_Context || ImGuiHasKeyboard())
			    {
				    return false;
			    }
			    return !m_Context->ProcessKeyDown(ToRml(e.m_KeyCode), m_Modifiers);
		    },
		    100));
		m_Connections.push_back(bus.Subscribe<KeyReleasedEvent>(
		    [this](const KeyReleasedEvent& e)
		    {
			    m_Modifiers &= ~ModifierOf(e.m_KeyCode);
			    if (!m_Context || ImGuiHasKeyboard())
			    {
				    return false;
			    }
			    return !m_Context->ProcessKeyUp(ToRml(e.m_KeyCode), m_Modifiers);
		    },
		    100));
		m_Connections.push_back(bus.Subscribe<KeyTypedEvent>(
		    [this](const KeyTypedEvent& e)
		    {
			    if (!m_Context || ImGuiHasKeyboard())
			    {
				    return false;
			    }
			    return !m_Context->ProcessTextInput(static_cast<Rml::Character>(e.m_KeyCode));
		    },
		    100));
	}

	Rml::Context* UiService::Context()
	{
		Initialise();
		return m_Context;
	}

	bool UiService::LoadFontFace(const std::filesystem::path& path, const bool fallback)
	{
		Initialise();
		if (!Rml::LoadFontFace(Utf8(path), fallback))
		{
			SS_CORE_WARN("UiService: font '{}' did not load", path.string());
			return false;
		}
		return true;
	}

	Rml::ElementDocument* UiService::LoadDocument(const std::filesystem::path& path)
	{
		Rml::Context* context = Context();
		return context ? context->LoadDocument(Utf8(path)) : nullptr;
	}

	void UiService::SetDpRatio(const float ratio)
	{
		if (Rml::Context* context = Context())
		{
			context->SetDensityIndependentPixelRatio(ratio);
		}
	}

	bool UiService::WantsMouse() const
	{
		return m_Context != nullptr && m_Context->IsMouseInteracting();
	}

	size_t UiService::PendingTextures() const
	{
		return m_Renderer ? m_Renderer->PendingTextures() : 0;
	}

	void UiService::BuildFrame(const uint32_t width, const uint32_t height)
	{
		if (!m_Context || width == 0 || height == 0)
		{
			return;
		}
		if (width != m_Width || height != m_Height)
		{
			m_Width = width;
			m_Height = height;
			m_Context->SetDimensions({static_cast<int>(width), static_cast<int>(height)});
		}
		m_Context->Update();
		m_Renderer->BeginFrame(width, height);
		m_Context->Render();
	}

	void UiService::Draw(CommandContext& ctx, const uint32_t frameIndex, const PixelFormat colorFormat)
	{
		if (m_Context && m_Renderer)
		{
			m_Renderer->Draw(ctx, frameIndex, colorFormat);
		}
	}

	void UiService::Shutdown()
	{
		if (!m_Initialised)
		{
			return;
		}
		m_Connections.clear();
		if (m_Context)
		{
			Rml::RemoveContext(m_Context->GetName());
			m_Context = nullptr;
		}
		Rml::Shutdown();
		m_Renderer.reset();
		m_System.reset();
		m_Initialised = false;
	}
}
