#pragma once

#include "Snowstorm/Core/PlatformDetection.hpp"

#ifdef SS_PLATFORM_WINDOWS
#ifndef NOMINMAX
// See github.com/skypjack/entt/wiki/Frequently-Asked-Questions#warning-c4003-the-min-the-max-and-the-macro
#define NOMINMAX
#endif
#endif

// ---------------------------------------------------------------------------------------------
// Precompiled header contents.
//
// Rule for what belongs here: a header that is (1) heavy to parse, (2) included by many TUs, and
// (3) stable / low-churn. Third-party library headers are the ideal fit — they never change, so
// force-including them into every Core TU costs nothing on edit but saves a full re-parse each
// build. We deliberately do NOT put volatile engine headers or include-order-sensitive backend
// headers (volk/Vulkan) here: editing a PCH'd header rebuilds ALL Core TUs, and the Vulkan loader
// headers must be included in a controlled order by the Platform/Vulkan TUs themselves.
// ---------------------------------------------------------------------------------------------

// --- C++ standard library (stable, widely used) ---
#include <iostream>
#include <memory>
#include <utility>
#include <algorithm>
#include <functional>
#include <optional>
#include <cstdint>
#include <cmath>

#include <string>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <array>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

// --- Heavy third-party template libraries (never change; the biggest parse-time wins) ---
// glm: maths, pulled in by ~20 Core TUs. entt: the ECS, one of the heaviest headers in the tree.
// rttr: reflection, force-included by every component TU. nlohmann/json: serialization.
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <rttr/registration.h>
#include <nlohmann/json.hpp>

// --- Engine headers that are low-churn and pulled in almost everywhere ---
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"       // -> spdlog + fmt (heavy)
#include "Snowstorm/Debug/Instrumentor.hpp"

#ifdef SS_PLATFORM_WINDOWS
#include <Windows.h>
#endif
