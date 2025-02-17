#pragma once

#include "Base.hpp"
#include "spdlog/spdlog.h"
#include "spdlog/fmt/ostr.h"

namespace Snowstorm
{
	class Log
	{
	public:
		static void Init();

		static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
		static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }

	private:
		static std::shared_ptr<spdlog::logger> s_CoreLogger;
		static std::shared_ptr<spdlog::logger> s_ClientLogger;
	};
}

// Core log macros
#define SS_CORE_TRACE(...)		::Snowstorm::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define SS_CORE_INFO(...)		::Snowstorm::Log::GetCoreLogger()->info(__VA_ARGS__)
#define SS_CORE_WARN(...)		::Snowstorm::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define SS_CORE_ERROR(...)		::Snowstorm::Log::GetCoreLogger()->error(__VA_ARGS__)
#define SS_CORE_CRITICAL(...)	::Snowstorm::Log::GetCoreLogger()->critical(__VA_ARGS__)

// Client log macros
#define SS_TRACE(...)			::Snowstorm::Log::GetClientLogger()->trace(__VA_ARGS__)
#define SS_INFO(...)			::Snowstorm::Log::GetClientLogger()->info(__VA_ARGS__)
#define SS_WARN(...)			::Snowstorm::Log::GetClientLogger()->warn(__VA_ARGS__)
#define SS_ERROR(...)			::Snowstorm::Log::GetClientLogger()->error(__VA_ARGS__)
#define SS_CRITICAL(...)		::Snowstorm::Log::GetClientLogger()->critical(__VA_ARGS__)
