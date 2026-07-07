#include "Application.hpp"

#include <GLFW/glfw3.h>

#include <ranges>

#include "Snowstorm/Core/CoreServices.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Debug/Instrumentor.hpp"
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

		// GPU subsystems (renderer + shader/mesh caches) are application-scoped and device-bound, so they
		// register here — after the device exists, before any World is created. Shared across all Worlds.
		RegisterCoreServices(*m_ServiceManager);

		// Component reflection (RTTR) + editor/serializer registration happen via per-component static
		// initializers (RTTR_REGISTRATION + AUTO_REGISTER_COMPONENT). Snowstorm-Core is a static lib, so
		// the executables link it WHOLE_ARCHIVE to keep those initializer TUs from being dropped — there
		// is no manual registration list to call here.
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

		// Smoke-test hook: if the smoke.frames CVar is a positive integer, run that many frames and
		// then exit cleanly. Lets automated smoke tests boot the app, exercise the init/update/
		// shutdown path, and return a real exit code without a human closing the window. Zero (the
		// normal case) -> runs until the window is closed.
		uint64_t smokeFramesLeft = 0;
		bool smokeMode = false;
		if (const int smokeFrames = CVars::SmokeFrames.Get(); smokeFrames > 0)
		{
			smokeMode = true;
			smokeFramesLeft = static_cast<uint64_t>(smokeFrames);
			SS_CORE_INFO("Smoke mode: running {0} frames then exiting.", smokeFramesLeft);
		}

		// Frame-time watchdog: when debug.max_frame_ms > 0, a single CPU frame exceeding it logs [error].
		// The smoke harness treats [error] as failure, so a per-frame stall (hitch/freeze) that the
		// N-frames-then-exit smoke would otherwise sail past becomes a hard, headless-reproducible failure.
		const int maxFrameMs = CVars::MaxFrameMs.Get();
		uint64_t frameNo = 0;

		// Headless profiler capture: if profile.capture_frames > 0, request a capture once a few frames of
		// warmup have passed (pipeline/asset init on frame 0-2 isn't representative of steady-state). This
		// makes the profiler driveable without the editor button — e.g. for smoke/offline trace analysis.
		const int profileCaptureFrames = CVars::ProfileCaptureFrames.Get();
		bool profileRequested = false;

		while (m_Running)
		{
			if (profileCaptureFrames > 0 && !profileRequested && frameNo == 3)
			{
				Instrumentor::Get().RequestCapture(profileCaptureFrames, CVars::ProfileCapturePath.Get());
				profileRequested = true;
			}

			// Drive on-demand frame capture (editor "Capture Frames" button). Must run before the frame
			// body so the whole frame's scopes are recorded; ends a capture once its frame budget elapses.
			Instrumentor::Get().OnFrameBoundary();

			SS_PROFILE_SCOPE("RunLoop");

			const double frameStart = glfwGetTime();
			const auto time = static_cast<float>(frameStart); // Platform::GetTime
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

			// Watchdog check. Frame 0 is exempt: one-time init (pipeline/shader/resource warmup) legitimately
			// takes longer and isn't the per-frame stall we're hunting.
			if (maxFrameMs > 0 && frameNo > 0)
			{
				const double frameMs = (glfwGetTime() - frameStart) * 1000.0;
				if (frameMs > static_cast<double>(maxFrameMs))
				{
					SS_CORE_ERROR("Frame-time watchdog: frame {0} took {1:.1f} ms (budget {2} ms)", frameNo, frameMs, maxFrameMs);
				}
			}
			++frameNo;

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
