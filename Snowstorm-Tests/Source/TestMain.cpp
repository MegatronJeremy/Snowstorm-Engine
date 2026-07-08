#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "Snowstorm/Core/Log.hpp"

// The engine's loggers (Log::s_CoreLogger / s_ClientLogger) are normally initialized by Log::Init() in
// the app entry point, which the Catch2 test runner never calls. Anything that logs — e.g. JobSystem's
// ctor does SS_CORE_INFO — would dereference a null logger and crash. Initialize logging once before any
// test runs via a Catch2 global listener, so tests can freely construct engine subsystems.
namespace
{
	struct LogInitListener final : Catch::EventListenerBase
	{
		using Catch::EventListenerBase::EventListenerBase;

		void testRunStarting(const Catch::TestRunInfo&) override
		{
			Snowstorm::Log::Init();
		}
	};
}

CATCH_REGISTER_LISTENER(LogInitListener)
