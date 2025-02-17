#pragma once

#include "Snowstorm/Core/PlatformDetection.hpp"

#include <memory>
#include <filesystem>

// TODO see what to do with this define
#ifdef _DEBUG
#define SS_DEBUG
#endif

#ifdef SS_DEBUG
#if defined(SS_PLATFORM_WINDOWS)
#define SS_DEBUGBREAK() __debugbreak()
#elif defined(SS_PLATFORM_LINUX)
#include <signal.h>
#define SS_DEBUGBREAK() raise(SIGTRAP)
#else
#error "Platform doesn't support debugbreak yet!"
#endif
#define SS_ENABLE_ASSERTS
#else
#define SS_DEBUGBREAK()
#endif

#ifdef SS_ENABLE_ASSERTS

// Alternatively we could use the same "default" message for both "WITH_MSG" and "NO_MSG" and
// provide support for custom formatting by concatenating the formatting string instead of having the format inside the default message
#define SS_INTERNAL_ASSERT_IMPL(type, check, msg, ...) { if(!(check)) { SS##type##ERROR(msg, __VA_ARGS__); SS_DEBUGBREAK(); } }
#define SS_INTERNAL_ASSERT_WITH_MSG(type, check, ...) SS_INTERNAL_ASSERT_IMPL(type, check, "Assertion failed: {0}", __VA_ARGS__)
#define SS_INTERNAL_ASSERT_NO_MSG(type, check) SS_INTERNAL_ASSERT_IMPL(type, check, "Assertion '{0}' failed at {1}:{2}", SS_STRINGIFY_MACRO(check), std::filesystem::path(__FILE__).filename().string(), __LINE__)

#define SS_INTERNAL_ASSERT_GET_MACRO_NAME(arg1, arg2, macro, ...) macro
#define SS_INTERNAL_ASSERT_GET_MACRO(...) SS_EXPAND_MACRO( SS_INTERNAL_ASSERT_GET_MACRO_NAME(__VA_ARGS__, SS_INTERNAL_ASSERT_WITH_MSG, SS_INTERNAL_ASSERT_NO_MSG) )

// Currently accepts at least the condition and one additional parameter (the message) being optional
#define SS_ASSERT(...) SS_EXPAND_MACRO( SS_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_, __VA_ARGS__) )
#define SS_CORE_ASSERT(...) SS_EXPAND_MACRO( SS_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_CORE_, __VA_ARGS__) )
#else
#define SS_ASSERT(...)
#define SS_CORE_ASSERT(...)
#endif


#define SS_EXPAND_MACRO(x) x
#define SS_STRINGIFY_MACRO(x) #x

#define BIT(x) (1 << (x))

#define CONCAT(x, y) x##y
#define C(x, y) CONCAT(x, y)

#define SS_BIND_EVENT_FN(fn) [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }

namespace Snowstorm
{
	template <typename T>
	using Scope = std::unique_ptr<T>;

	template <typename T, typename... Args>
	constexpr Scope<T> CreateScope(Args&&... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	template <typename T>
	using Ref = std::shared_ptr<T>;

	template <typename T, typename... Args>
	constexpr Ref<T> CreateRef(Args&&... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}
}
