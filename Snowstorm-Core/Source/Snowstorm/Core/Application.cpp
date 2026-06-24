#include "pch.h"
#include "Application.hpp"

#include <GLFW/glfw3.h>

#include <cstdlib>
#include <ranges>

#include "Snowstorm/Components/ComponentRegistration.hpp"
#include "Snowstorm/Render/Renderer.hpp"

namespace Snowstorm
{
	Application* Application::s_Instance = nullptr;

	Application::Application(const std::string& name)
	{
		SS_PROFILE_FUNCTION();

		SS_CORE_ASSERT(!s_Instance, "Application already exists!");
		s_Instance = this;

		m_Window = Window::Create(WindowProps(name));
		m_Window->SetEventCallback(SS_BIND_EVENT_FN(OnEvent));

		m_EventBus = CreateScope<EventBus>();

		// TODO think about this (but it's probably fine)
		m_ServiceManager = CreateScope<ServiceManager>();

		m_LayerStack = CreateScope<LayerStack>();

		Renderer::Init(m_Window->GetNativeWindow());

		InitializeRTTR();
	}

	Application::~Application()
	{
		SS_PROFILE_FUNCTION();

		// Finish all in-flight GPU work before tearing down layers/worlds, which destroy GPU
		// resources (textures, descriptor sets, views). Without this they can be destroyed while
		// still referenced by the last submitted command buffer -> "in use by command buffer"
		// validation errors on shutdown.
		Renderer::WaitIdle();

		m_LayerStack.reset();

		Renderer::ShutdownImGuiBackend();

		m_ServiceManager.reset();

		m_Window.reset();
		Renderer::Shutdown();
	}

	void Application::Run()
	{
		SS_PROFILE_FUNCTION();

		// Smoke-test hook: if SS_SMOKE_FRAMES is set to a positive integer, run that many
		// frames and then exit cleanly. Lets automated smoke tests boot the app, exercise the
		// init/update/shutdown path, and return a real exit code without a human closing the
		// window. Unset (the normal case) -> runs until the window is closed.
		uint64_t smokeFramesLeft = 0;
		bool smokeMode = false;
		if (const char* smokeEnv = std::getenv("SS_SMOKE_FRAMES"))
		{
			const long long parsed = std::strtoll(smokeEnv, nullptr, 10);
			if (parsed > 0)
			{
				smokeMode = true;
				smokeFramesLeft = static_cast<uint64_t>(parsed);
				SS_CORE_INFO("Smoke mode: running {0} frames then exiting.", smokeFramesLeft);
			}
		}

		while (m_Running)
		{
			SS_PROFILE_SCOPE("RunLoop");

			const auto time = static_cast<float>(glfwGetTime()); // Platform::GetTime
			const Timestep ts = time - m_LastFrameTime;
			m_LastFrameTime = time;

			// Pause when minimized
			if (!m_Minimized)
			{
				SS_PROFILE_SCOPE("LayerStack OnUpdate");

				m_ServiceManager->ExecuteUpdate(ts);

				for (Layer* layer : *m_LayerStack)
				{
					layer->OnUpdate(ts);
				}

				m_ServiceManager->ExecutePostUpdate(ts);
			}

			m_Window->OnUpdate();

			if (smokeMode && --smokeFramesLeft == 0)
			{
				SS_CORE_INFO("Smoke mode: frame budget reached, requesting shutdown.");
				m_Running = false;
			}
		}
	}

	void Application::OnEvent(Event& e)
	{
		SS_PROFILE_FUNCTION();

		// Core app handling first (no EventDispatcher)
		switch (e.GetEventType())
		{
		case EventType::WindowClose:
		{
			m_Running = false;
			e.Handled = true;
			break;
		}
		case EventType::WindowResize:
		{
			const auto& re = static_cast<WindowResizeEvent&>(e);

			m_Window->Resize(re.Width, re.Height);

			if (re.Width == 0 || re.Height == 0)
			{
				m_Minimized = true;
			}
			else
			{
				m_Minimized = false;
			}

			// Let others also react if they want; do NOT force handled here unless you truly want to.
			// e.Handled = true; // optional
			break;
		}
		default:
			break;
		}

		// Always publish (subscribers decide what to do)
		m_EventBus->Publish(e);
	}

	void Application::PushLayer(Layer* layer) const
	{
		SS_PROFILE_FUNCTION();

		m_LayerStack->PushLayer(layer);
		layer->OnAttach();
	}

	void Application::Close()
	{
		m_Running = false;
	}
}
