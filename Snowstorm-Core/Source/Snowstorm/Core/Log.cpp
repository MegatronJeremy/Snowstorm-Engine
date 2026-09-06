#include "Log.hpp"

#include "LogBuffer.hpp"

#include "spdlog/sinks/stdout_color_sinks.h"

namespace Snowstorm
{
	std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

	void Log::Init()
	{
		// Include the level name (%l) so logs are machine-greppable (e.g. by the smoke test),
		// not just colour-coded. Format: [HH:MM:SS] [level] LOGGER: message
		spdlog::set_pattern("%^[%T] [%l] %n: %v%$");
		s_CoreLogger = spdlog::stdout_color_mt("SNOWSTORM");
		s_CoreLogger->set_level(spdlog::level::trace);

		s_ClientLogger = spdlog::stdout_color_mt("APP");
		s_ClientLogger->set_level(spdlog::level::trace);

		// Flush every error and above immediately. An assertion logs the reason and then breaks into the
		// debugger, and a break with no debugger attached kills the process; with stdout redirected to a
		// pipe or a file it is block-buffered, so the message explaining the crash is exactly the output
		// that gets discarded. That turns a one-line diagnosis into a bisect. Info and below stay buffered,
		// since they are the per-frame bulk and flushing each one is a measurable cost.
		spdlog::flush_on(spdlog::level::err);

		// Add an in-memory sink (alongside stdout) so the editor's Console panel can show the log stream.
		// Harmless in headless/runtime builds — it just captures into a bounded buffer nobody reads.
		InstallLogBufferSink();
	}
}
