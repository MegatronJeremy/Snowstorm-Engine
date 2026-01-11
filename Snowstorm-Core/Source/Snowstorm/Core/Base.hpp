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

#define SS_INTERNAL_ASSERT_IMPL(type, check, ...)                                                           \
do                                                                                                      \
{                                                                                                       \
if (!(check))                                                                                       \
{                                                                                                   \
SS##type##ERROR(__VA_ARGS__);                                                                   \
SS_DEBUGBREAK();                                                                                \
}                                                                                                   \
} while (false)

#define SS_INTERNAL_ASSERT_WITH_MSG(type, check, ...) SS_INTERNAL_ASSERT_IMPL(type, check, __VA_ARGS__)
#define SS_INTERNAL_ASSERT_NO_MSG(type, check) SS_INTERNAL_ASSERT_IMPL(type, check, "Assertion '{0}' failed at {1}:{2}", SS_STRINGIFY_MACRO(check), std::filesystem::path(__FILE__).filename().string(), __LINE__)

// Robust: if only 1 arg -> NO_MSG, otherwise -> WITH_MSG (even for 3+ args)
#define SS_INTERNAL_ASSERT_GET_MACRO_NAME(_1, _2, _3, _4, NAME, ...) NAME
#define SS_INTERNAL_ASSERT_GET_MACRO(...) SS_EXPAND_MACRO(SS_INTERNAL_ASSERT_GET_MACRO_NAME(__VA_ARGS__, SS_INTERNAL_ASSERT_WITH_MSG, SS_INTERNAL_ASSERT_WITH_MSG, SS_INTERNAL_ASSERT_WITH_MSG, SS_INTERNAL_ASSERT_NO_MSG))

#define SS_ASSERT(...) SS_EXPAND_MACRO(SS_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_, __VA_ARGS__))
#define SS_CORE_ASSERT(...) SS_EXPAND_MACRO(SS_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_CORE_, __VA_ARGS__))

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
