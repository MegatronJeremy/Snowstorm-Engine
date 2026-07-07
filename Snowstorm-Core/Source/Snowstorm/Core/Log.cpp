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

		// Add an in-memory sink (alongside stdout) so the editor's Console panel can show the log stream.
		// Harmless in headless/runtime builds — it just captures into a bounded buffer nobody reads.
		InstallLogBufferSink();
	}
}
