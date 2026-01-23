#pragma once

#include "Snowstorm/Core/PlatformDetection.hpp"

#ifdef SS_PLATFORM_WINDOWS
#ifndef NOMINMAX
// See github.com/skypjack/entt/wiki/Frequently-Asked-Questions#warning-c4003-the-min-the-max-and-the-macro
#define NOMINMAX
#endif
#endif

#include <iostream>
#include <memory>
#include <utility>
#include <algorithm>
#include <functional>

#include <string>
#include <sstream>
#include <array>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "Snowstorm/Core/Base.hpp"

#include "Snowstorm/Core/Log.hpp"

#include "Snowstorm/Debug/Instrumentor.hpp"

#ifdef SS_PLATFORM_WINDOWS
#include <Windows.h>
#endif
