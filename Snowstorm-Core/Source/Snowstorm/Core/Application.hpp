#pragma once

#include "Window.hpp"
#include "Snowstorm/Core/LayerStack.hpp"
#include "Snowstorm/Events/Event.hpp"
#include "Snowstorm/Events/ApplicationEvent.hpp"
#include "Snowstorm/Events/EventBus.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

namespace Snowstorm
{
	class Application : public NonCopyable
	{
	public:
		explicit Application(const std::string& name = "Snowstorm App");
		~Application() override;

		void Run();

		void OnEvent(Event& e);

		void PushLayer(Layer* layer) const;

		Window& GetWindow() const { return *m_Window; }

		void Close();

		static Application& Get() { return *s_Instance; }

		ServiceManager& GetServiceManager() const { return *m_ServiceManager; }

		EventBus& GetEventBus() const { return *m_EventBus; }

	protected:
		Scope<ServiceManager> m_ServiceManager;

	private:
		bool OnWindowClose(WindowCloseEvent& e);
		bool OnWindowResize(WindowResizeEvent& e);

		Scope<Window> m_Window;
		Scope<EventBus> m_EventBus;
		bool m_Running = true;
		bool m_Minimized = false;
		Scope<LayerStack> m_LayerStack;
		float m_LastFrameTime = 0.0f;

		static Application* s_Instance;
	};

	// To be defined in CLIENT
	Application* CreateApplication();
}
