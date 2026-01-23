#include "pch.h"
#include "Application.hpp"

#include <GLFW/glfw3.h>

#include <ranges>

#include "Snowstorm/Components/ComponentRegistration.hpp"
#include "Snowstorm/Render/Renderer.hpp"

namespace Snowstorm
{
#define BIND_EVENT_FN(x) [this](auto&&... args) { return this->x(std::forward<decltype(args)>(args)...); }

	Application* Application::s_Instance = nullptr;

	Application::Application(const std::string& name)
	{
		SS_PROFILE_FUNCTION();

		SS_CORE_ASSERT(!s_Instance, "Application already exists!");
		s_Instance = this;

		m_Window = Window::Create(WindowProps(name));
		m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));

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

		m_LayerStack.reset();

		Renderer::ShutdownImGuiBackend();

		m_ServiceManager.reset();

		m_Window.reset();
		Renderer::Shutdown();
	}

	void Application::Run()
	{
		SS_PROFILE_FUNCTION();

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

	bool Application::OnWindowClose(WindowCloseEvent& e)
	{
		m_Running = false;
		return true;
	}

	bool Application::OnWindowResize(WindowResizeEvent& e)
	{
		SS_PROFILE_FUNCTION();

		m_Window->Resize(e.Width, e.Height);

		e.Handled = true;
		if (e.Width == 0 || e.Height == 0)
		{
			m_Minimized = true;
			return true;
		}

		m_Minimized = false;
		return true;
	}
}
