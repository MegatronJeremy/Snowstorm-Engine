#include <catch2/catch_test_macros.hpp>

#include <catch2/catch_approx.hpp>

#include <GLFW/glfw3.h>

#include "Snowstorm/Core/KeyCodes.hpp"
#include "Snowstorm/Core/MouseCodes.hpp"
#include "Snowstorm/Core/Timestep.hpp"

#include <algorithm>
#include <type_traits>
#include <vector>

using namespace Snowstorm;

// Key:: values are GLFW's, the input bridge forwards them unmapped.
#define SS_KEY_TABLE(X)                     \
	X(Space, GLFW_KEY_SPACE)                \
	X(Apostrophe, GLFW_KEY_APOSTROPHE)      \
	X(Comma, GLFW_KEY_COMMA)                \
	X(Minus, GLFW_KEY_MINUS)                \
	X(Period, GLFW_KEY_PERIOD)              \
	X(Slash, GLFW_KEY_SLASH)                \
	X(D0, GLFW_KEY_0)                       \
	X(D1, GLFW_KEY_1)                       \
	X(D2, GLFW_KEY_2)                       \
	X(D3, GLFW_KEY_3)                       \
	X(D4, GLFW_KEY_4)                       \
	X(D5, GLFW_KEY_5)                       \
	X(D6, GLFW_KEY_6)                       \
	X(D7, GLFW_KEY_7)                       \
	X(D8, GLFW_KEY_8)                       \
	X(D9, GLFW_KEY_9)                       \
	X(Semicolon, GLFW_KEY_SEMICOLON)        \
	X(Equal, GLFW_KEY_EQUAL)                \
	X(A, GLFW_KEY_A)                        \
	X(B, GLFW_KEY_B)                        \
	X(C, GLFW_KEY_C)                        \
	X(D, GLFW_KEY_D)                        \
	X(E, GLFW_KEY_E)                        \
	X(F, GLFW_KEY_F)                        \
	X(G, GLFW_KEY_G)                        \
	X(H, GLFW_KEY_H)                        \
	X(I, GLFW_KEY_I)                        \
	X(J, GLFW_KEY_J)                        \
	X(K, GLFW_KEY_K)                        \
	X(L, GLFW_KEY_L)                        \
	X(M, GLFW_KEY_M)                        \
	X(N, GLFW_KEY_N)                        \
	X(O, GLFW_KEY_O)                        \
	X(P, GLFW_KEY_P)                        \
	X(Q, GLFW_KEY_Q)                        \
	X(R, GLFW_KEY_R)                        \
	X(S, GLFW_KEY_S)                        \
	X(T, GLFW_KEY_T)                        \
	X(U, GLFW_KEY_U)                        \
	X(V, GLFW_KEY_V)                        \
	X(W, GLFW_KEY_W)                        \
	X(X, GLFW_KEY_X)                        \
	X(Y, GLFW_KEY_Y)                        \
	X(Z, GLFW_KEY_Z)                        \
	X(LeftBracket, GLFW_KEY_LEFT_BRACKET)   \
	X(Backslash, GLFW_KEY_BACKSLASH)        \
	X(RightBracket, GLFW_KEY_RIGHT_BRACKET) \
	X(GraveAccent, GLFW_KEY_GRAVE_ACCENT)   \
	X(World1, GLFW_KEY_WORLD_1)             \
	X(World2, GLFW_KEY_WORLD_2)             \
	X(Escape, GLFW_KEY_ESCAPE)              \
	X(Enter, GLFW_KEY_ENTER)                \
	X(Tab, GLFW_KEY_TAB)                    \
	X(Backspace, GLFW_KEY_BACKSPACE)        \
	X(Insert, GLFW_KEY_INSERT)              \
	X(Delete, GLFW_KEY_DELETE)              \
	X(Right, GLFW_KEY_RIGHT)                \
	X(Left, GLFW_KEY_LEFT)                  \
	X(Down, GLFW_KEY_DOWN)                  \
	X(Up, GLFW_KEY_UP)                      \
	X(PageUp, GLFW_KEY_PAGE_UP)             \
	X(PageDown, GLFW_KEY_PAGE_DOWN)         \
	X(Home, GLFW_KEY_HOME)                  \
	X(End, GLFW_KEY_END)                    \
	X(CapsLock, GLFW_KEY_CAPS_LOCK)         \
	X(ScrollLock, GLFW_KEY_SCROLL_LOCK)     \
	X(NumLock, GLFW_KEY_NUM_LOCK)           \
	X(PrintScreen, GLFW_KEY_PRINT_SCREEN)   \
	X(Pause, GLFW_KEY_PAUSE)                \
	X(F1, GLFW_KEY_F1)                      \
	X(F2, GLFW_KEY_F2)                      \
	X(F3, GLFW_KEY_F3)                      \
	X(F4, GLFW_KEY_F4)                      \
	X(F5, GLFW_KEY_F5)                      \
	X(F6, GLFW_KEY_F6)                      \
	X(F7, GLFW_KEY_F7)                      \
	X(F8, GLFW_KEY_F8)                      \
	X(F9, GLFW_KEY_F9)                      \
	X(F10, GLFW_KEY_F10)                    \
	X(F11, GLFW_KEY_F11)                    \
	X(F12, GLFW_KEY_F12)                    \
	X(F13, GLFW_KEY_F13)                    \
	X(F14, GLFW_KEY_F14)                    \
	X(F15, GLFW_KEY_F15)                    \
	X(F16, GLFW_KEY_F16)                    \
	X(F17, GLFW_KEY_F17)                    \
	X(F18, GLFW_KEY_F18)                    \
	X(F19, GLFW_KEY_F19)                    \
	X(F20, GLFW_KEY_F20)                    \
	X(F21, GLFW_KEY_F21)                    \
	X(F22, GLFW_KEY_F22)                    \
	X(F23, GLFW_KEY_F23)                    \
	X(F24, GLFW_KEY_F24)                    \
	X(F25, GLFW_KEY_F25)                    \
	X(KP0, GLFW_KEY_KP_0)                   \
	X(KP1, GLFW_KEY_KP_1)                   \
	X(KP2, GLFW_KEY_KP_2)                   \
	X(KP3, GLFW_KEY_KP_3)                   \
	X(KP4, GLFW_KEY_KP_4)                   \
	X(KP5, GLFW_KEY_KP_5)                   \
	X(KP6, GLFW_KEY_KP_6)                   \
	X(KP7, GLFW_KEY_KP_7)                   \
	X(KP8, GLFW_KEY_KP_8)                   \
	X(KP9, GLFW_KEY_KP_9)                   \
	X(KPDecimal, GLFW_KEY_KP_DECIMAL)       \
	X(KPDivide, GLFW_KEY_KP_DIVIDE)         \
	X(KPMultiply, GLFW_KEY_KP_MULTIPLY)     \
	X(KPSubtract, GLFW_KEY_KP_SUBTRACT)     \
	X(KPAdd, GLFW_KEY_KP_ADD)               \
	X(KPEnter, GLFW_KEY_KP_ENTER)           \
	X(KPEqual, GLFW_KEY_KP_EQUAL)           \
	X(LeftShift, GLFW_KEY_LEFT_SHIFT)       \
	X(LeftControl, GLFW_KEY_LEFT_CONTROL)   \
	X(LeftAlt, GLFW_KEY_LEFT_ALT)           \
	X(LeftSuper, GLFW_KEY_LEFT_SUPER)       \
	X(RightShift, GLFW_KEY_RIGHT_SHIFT)     \
	X(RightControl, GLFW_KEY_RIGHT_CONTROL) \
	X(RightAlt, GLFW_KEY_RIGHT_ALT)         \
	X(RightSuper, GLFW_KEY_RIGHT_SUPER)     \
	X(Menu, GLFW_KEY_MENU)

#define X(name, glfw) static_assert(Key::name == (glfw), "Key::" #name " drifted from " #glfw);
SS_KEY_TABLE(X)
#undef X

static_assert(Mouse::Button0 == GLFW_MOUSE_BUTTON_1);
static_assert(Mouse::Button7 == GLFW_MOUSE_BUTTON_8);
static_assert(Mouse::ButtonLast == GLFW_MOUSE_BUTTON_LAST);
static_assert(Mouse::ButtonLeft == GLFW_MOUSE_BUTTON_LEFT);
static_assert(Mouse::ButtonRight == GLFW_MOUSE_BUTTON_RIGHT);
static_assert(Mouse::ButtonMiddle == GLFW_MOUSE_BUTTON_MIDDLE);

TEST_CASE("Key codes equal the GLFW constants at runtime", "[input]")
{
	std::size_t count = 0;
#define X(name, glfw)                               \
	REQUIRE(static_cast<int>(Key::name) == (glfw)); \
	++count;
	SS_KEY_TABLE(X)
#undef X
	REQUIRE(count > 100);
}

TEST_CASE("Key codes are unique", "[input]")
{
	std::vector<int> codes;
#define X(name, glfw) codes.push_back(static_cast<int>(Key::name));
	SS_KEY_TABLE(X)
#undef X
	std::sort(codes.begin(), codes.end());
	REQUIRE(std::adjacent_find(codes.begin(), codes.end()) == codes.end());
}

TEST_CASE("Mouse button aliases name the first three buttons", "[input]")
{
	REQUIRE(Mouse::ButtonLeft == Mouse::Button0);
	REQUIRE(Mouse::ButtonRight == Mouse::Button1);
	REQUIRE(Mouse::ButtonMiddle == Mouse::Button2);
	REQUIRE(Mouse::ButtonLast == Mouse::Button7);
}

TEST_CASE("Timestep reports seconds and milliseconds", "[core]")
{
	const Timestep ts(0.016f);
	REQUIRE(ts.GetSeconds() == 0.016f);
	REQUIRE(ts.GetMilliseconds() == Catch::Approx(16.0f));
	REQUIRE(Timestep().GetSeconds() == 0.0f);
	REQUIRE(Timestep(2.5f).GetMilliseconds() == Catch::Approx(2500.0f));
}

TEST_CASE("Timestep converts implicitly to float", "[core]")
{
	const Timestep ts(0.5f);
	const float f = ts;
	REQUIRE(f == 0.5f);
	REQUIRE(ts * 4.0f == 2.0f);
	static_assert(std::is_convertible_v<Timestep, float>);
	static_assert(std::is_convertible_v<float, Timestep>);
}
