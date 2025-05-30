#include "pch.h"
#include "Application.hpp"

#include <GLFW/glfw3.h>

#include <ranges>

#include "Snowstorm/Components/ComponentRegistration.hpp"
#include "Snowstorm/Render/RenderCommand.hpp"
#include "Snowstorm/Render/Renderer2D.hpp"

namespace Snowstorm
{
#define BIND_EVENT_FN(x) [this](auto&&... args) { return this->x(std::forward<decltype(args)>(args)...); }

	Application* Application::s_Instance = nullptr;

	Application::Application(const std::string& name)
	{
		SS_PROFILE_FUNCTION();

		SS_CORE_ASSERT(!s_Instance, "Application already exists!")
		s_Instance = this;

		m_Window = Window::Create(WindowProps(name));
		m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));

		// TODO think about this (but it's probably fine)
		m_ServiceManager = CreateScope<ServiceManager>();

		// TODO these should be services (which have callable methods -> sort of like singletons, you can globally fetch a service through instance())
		Renderer2D::Init();
		RenderCommand::Init();

		InitializeRTTR();
	}

	Application::~Application()
	{
		SS_PROFILE_FUNCTION();

		Renderer2D::Shutdown();

		m_ServiceManager.reset(); // TODO kind of a hack in order to guarantee correct shutdown order
		m_Window.reset();
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

				for (Layer* layer : m_LayerStack)
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

		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(OnWindowClose));
		dispatcher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(OnWindowResize));

		if (e.Handled)
		{
			return;
		}

		for (const auto& it : std::ranges::reverse_view(m_LayerStack))
		{
			it->OnEvent(e);
		}
	}

	void Application::PushLayer(Layer* layer)
	{
		SS_PROFILE_FUNCTION();

		m_LayerStack.PushLayer(layer);
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
