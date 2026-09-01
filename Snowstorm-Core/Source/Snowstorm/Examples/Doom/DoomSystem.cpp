#include "DoomSystem.hpp"

#include "DoomComponent.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/KeyCodes.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Events/KeyEvent.hpp"
#include "Snowstorm/Input/InputStateSingleton.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/Utility/CVar.hpp"

#include <filesystem>
#include <utility>

#ifdef SS_HAS_DOOM
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

// Pulled in first so their include guards are already set: doomgeneric.h includes both, and letting a
// standard header expand inside the extern "C" below would give its declarations C linkage.
#include <stdint.h>
#include <stdlib.h>

// doomgeneric.h declares DG_ScreenBuffer OUTSIDE its own extern "C" block, so including it bare from C++
// gives that symbol C++ linkage and it fails to resolve against the C definition. Wrapping the whole
// header fixes it; the nested extern "C" inside is legal.
extern "C"
{
#include <doomgeneric.h>
#include <doomkeys.h>
}
#endif

namespace Snowstorm
{
	namespace
	{
		CVar<bool> DoomEnabled{"doom.enabled", false,
		                       "Run the embedded Doom interpreter and show its framebuffer on the material named by a DoomComponent. Requires a build configured with -DSS_ENABLE_DOOM=ON and a doom.wad path.",
		                       CVarFlags::Persist};

		CVar<std::string> DoomWad{"doom.wad", "",
		                          "IWAD the embedded Doom loads (e.g. freedoom1.wad), relative to the working directory. Empty or missing = Doom does not start. Read once when the interpreter initialises.",
		                          CVarFlags::Persist | CVarFlags::ReadOnly};
	}

#ifdef SS_HAS_DOOM
	namespace
	{
		struct KeyEventRecord
		{
			bool Pressed;
			unsigned char Key;
		};

		// Everything the Doom thread and the engine share, including the argv strings, because doomgeneric
		// stores the argv POINTERS rather than copying them (doomgeneric.c: myargv = argv).
		struct DoomShared
		{
			std::mutex FrameMutex;
			std::vector<uint32_t> Frame;
			bool HasFrame = false;

			std::mutex KeyMutex;
			std::deque<KeyEventRecord> Keys;

			std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();

			std::string ExeArg = "snowstorm";
			std::string IwadFlag = "-iwad";
			std::string WadArg;
			std::array<char*, 3> Argv{};
		};

		// Leaked on purpose, and this is load-bearing. Doom has no shutdown entry point, so its thread is
		// detached and runs until the process dies; it is still calling DG_GetTicksMs and DG_DrawFrame
		// while the CRT runs static destructors on the main thread. A shared_ptr or any object with a
		// destructor here would be torn down under that thread. Nothing this block owns is ever freed, so
		// there is nothing for the thread to read after its lifetime ends.
		DoomShared* g_Doom = nullptr;

		// doomgeneric holds its state in globals and D_DoomMain runs once, so a second create would corrupt
		// it. One interpreter per process, deliberately outliving any World.
		bool g_DoomStarted = false;

		constexpr uint32_t kDoomWidth = DOOMGENERIC_RESX;
		constexpr uint32_t kDoomHeight = DOOMGENERIC_RESY;
		constexpr uint32_t kDoomPixels = kDoomWidth * kDoomHeight;
		constexpr uint32_t kDoomBytes = kDoomPixels * 4;

		// Engine (GLFW) keycode -> Doom keycode. The two spaces agree on printable ASCII and diverge
		// everywhere above it: Escape is 256 here and 27 in Doom, and Doom's arrows and modifiers sit in a
		// private 0xa0..0xaf block that means nothing to GLFW. Everything absent maps to 0, which
		// doomgeneric ignores.
		std::array<unsigned char, InputStateSingleton::MaxKeys> BuildKeyMap()
		{
			std::array<unsigned char, InputStateSingleton::MaxKeys> map{};

			for (int k = Key::A; k <= Key::Z; ++k)
			{
				map[k] = static_cast<unsigned char>('a' + (k - Key::A)); // Doom expects lowercase ASCII
			}
			for (int k = Key::D0; k <= Key::D9; ++k)
			{
				map[k] = static_cast<unsigned char>('0' + (k - Key::D0));
			}

			map[Key::Right] = KEY_RIGHTARROW;
			map[Key::Left] = KEY_LEFTARROW;
			map[Key::Up] = KEY_UPARROW;
			map[Key::Down] = KEY_DOWNARROW;
			map[Key::Escape] = KEY_ESCAPE;
			map[Key::Enter] = KEY_ENTER;
			map[Key::KPEnter] = KEY_ENTER;
			map[Key::Tab] = KEY_TAB;
			map[Key::Backspace] = KEY_BACKSPACE;
			map[Key::Pause] = KEY_PAUSE;
			map[Key::Space] = ' ';
			map[Key::Minus] = KEY_MINUS;
			map[Key::Equal] = KEY_EQUALS;

			map[Key::LeftControl] = KEY_FIRE; // Doom's default fire
			map[Key::RightControl] = KEY_FIRE;
			map[Key::LeftShift] = KEY_RSHIFT; // run
			map[Key::RightShift] = KEY_RSHIFT;
			map[Key::LeftAlt] = KEY_LALT; // strafe modifier
			map[Key::RightAlt] = KEY_RALT;

			map[Key::F1] = KEY_F1;
			map[Key::F2] = KEY_F2;
			map[Key::F3] = KEY_F3;
			map[Key::F4] = KEY_F4;
			map[Key::F5] = KEY_F5;
			map[Key::F6] = KEY_F6;
			map[Key::F7] = KEY_F7;
			map[Key::F8] = KEY_F8;
			map[Key::F9] = KEY_F9;
			map[Key::F10] = KEY_F10;
			map[Key::F11] = KEY_F11;
			map[Key::F12] = KEY_F12;

			return map;
		}

		const std::array<unsigned char, InputStateSingleton::MaxKeys>& KeyMap()
		{
			static const auto map = BuildKeyMap();
			return map;
		}

		void PushKey(const int engineKey, const bool pressed)
		{
			if (!g_Doom || engineKey < 0 || std::cmp_greater_equal(engineKey, InputStateSingleton::MaxKeys))
			{
				return;
			}
			const unsigned char doomKey = KeyMap()[engineKey];
			if (doomKey == 0)
			{
				return;
			}

			std::lock_guard lock(g_Doom->KeyMutex);
			// Doom drains at 35 Hz and the engine can produce faster than that while stalled; drop the
			// oldest rather than grow without bound. A queue this deep is already several seconds of input.
			constexpr size_t maxQueued = 256;
			if (g_Doom->Keys.size() >= maxQueued)
			{
				g_Doom->Keys.pop_front();
			}
			g_Doom->Keys.push_back({pressed, doomKey});
		}
	}
}

// doomgeneric's platform layer. Called on the Doom thread, except that the key queue is filled from the
// engine thread under KeyMutex.
extern "C"
{
	void DG_Init()
	{
	}

	void DG_DrawFrame()
	{
		using namespace Snowstorm;
		if (!g_Doom || DG_ScreenBuffer == nullptr)
		{
			return;
		}
		std::lock_guard lock(g_Doom->FrameMutex);
		std::memcpy(g_Doom->Frame.data(), DG_ScreenBuffer, kDoomBytes);
		g_Doom->HasFrame = true;
	}

	void DG_SleepMs(const uint32_t ms)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
	}

	uint32_t DG_GetTicksMs()
	{
		using namespace Snowstorm;
		if (!g_Doom)
		{
			return 0;
		}
		const auto elapsed = std::chrono::steady_clock::now() - g_Doom->Start;
		return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
	}

	int DG_GetKey(int* pressed, unsigned char* key)
	{
		using namespace Snowstorm;
		if (!g_Doom)
		{
			return 0;
		}
		std::lock_guard lock(g_Doom->KeyMutex);
		if (g_Doom->Keys.empty())
		{
			return 0;
		}
		const KeyEventRecord e = g_Doom->Keys.front();
		g_Doom->Keys.pop_front();
		*pressed = e.Pressed ? 1 : 0;
		*key = e.Key;
		return 1;
	}

	void DG_SetWindowTitle(const char*)
	{
	}
}

namespace Snowstorm
{
	namespace
	{
		// An IWAD that merely exists is not enough: D_FindWADByName accepts any existing file, and w_wad.c
		// then I_Errors on a bad header, which on Windows pops a modal MessageBox and calls exit(-1) from
		// the Doom thread, taking the editor with it. Checking the 4-byte magic here keeps the common
		// mistakes (a truncated download, a renamed archive, a PK3) a warning instead of a process kill.
		bool LooksLikeWad(const std::filesystem::path& path)
		{
			std::ifstream f(path, std::ios::binary);
			char magic[4] = {};
			if (!f.read(magic, 4))
			{
				return false;
			}
			return std::memcmp(magic, "IWAD", 4) == 0 || std::memcmp(magic, "PWAD", 4) == 0;
		}

		void StartDoomThread(const std::string& wadPath)
		{
			g_Doom = new DoomShared();
			g_Doom->Frame.assign(kDoomPixels, 0);
			g_Doom->WadArg = wadPath;
			g_Doom->Argv = {g_Doom->ExeArg.data(), g_Doom->IwadFlag.data(), g_Doom->WadArg.data()};

			// Detached: doomgeneric_Tick never returns and there is no shutdown entry point, so the thread
			// ends with the process. Everything it touches is either doomgeneric's own globals or the
			// leaked block above, so nothing it can reach is destroyed while it runs.
			std::thread(
			    []
			    {
				    doomgeneric_Create(static_cast<int>(g_Doom->Argv.size()), g_Doom->Argv.data());
				    while (true)
				    {
					    doomgeneric_Tick();
				    }
			    })
			    .detach();

			g_DoomStarted = true;
		}
	}
#endif // SS_HAS_DOOM

	DoomSystem::DoomSystem(const WorldRef world)
	    : System(world)
	{
#ifdef SS_HAS_DOOM
		if (!Application::Exists())
		{
			return;
		}

		// Subscribed directly to the bus rather than read from InputStateSingleton: its per-frame edge
		// bitsets carry no ordering and collapse a press and release that land in the same frame, which is
		// exactly what DG_GetKey's queue must not lose.
		auto& bus = Application::Get().GetEventBus();

		m_KeyDown = bus.Subscribe<KeyPressedEvent>(
		    [this](const KeyPressedEvent& e)
		    {
			    // Auto-repeat would enqueue a second press with no release between; Doom tracks key state
			    // itself and only wants the real edges.
			    if (e.m_RepeatCount == 0 && !SingletonView<InputStateSingleton>().WantTextInput)
			    {
				    PushKey(e.m_KeyCode, true);
			    }
			    return false; // never mark handled: editor shortcuts and camera control run after this
		    },
		    0);

		m_KeyUp = bus.Subscribe<KeyReleasedEvent>(
		    [this](const KeyReleasedEvent& e)
		    {
			    if (!SingletonView<InputStateSingleton>().WantTextInput)
			    {
				    PushKey(e.m_KeyCode, false);
			    }
			    return false;
		    },
		    0);
#endif
	}

	DoomSystem::~DoomSystem() = default;

	bool DoomSystem::EnsureResources(const entt::entity entity, const AssetHandle material)
	{
#ifdef SS_HAS_DOOM
		auto& assets = SingletonView<AssetManagerSingleton>();
		const auto& instance = assets.GetMaterialInstance(material);
		if (!instance)
		{
			return false; // expected transient while the material's shader compiles
		}

		if (!m_Screen)
		{
			TextureDesc desc{};
			desc.Format = PixelFormat::BGRA8_sRGB; // doomgeneric writes XRGB8888, which is B,G,R,X in memory
			desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;
			desc.Width = kDoomWidth;
			desc.Height = kDoomHeight;
			desc.MipLevels = 1;
			desc.DebugName = "DoomScreen";

			m_Screen = Texture::Create(desc);
			m_ScreenView = m_Screen->GetDefaultView();
			m_Latest.assign(kDoomPixels, 0);
		}

		// Checked against the instance every frame rather than latched once. Saving the material in the
		// inspector calls ReloadMaterial, which evicts the cached instance; the rebuilt one gets albedo 0
		// from the .ssmat (which declares no texture) and the quad would go black forever. Comparing the
		// bindless index re-applies the takeover instead. Cheap: bindless slots are assigned once at view
		// creation and never recycled, so this is an integer compare in the steady state.
		if (instance->GetConstants().AlbedoTextureIndex != m_ScreenView->GetGlobalBindlessIndex())
		{
			instance->SetAlbedoTexture(m_ScreenView);

			// The RT geometry table caches each instance's albedo index and is only rebuilt when the ECS
			// reports a change (TlasBuildSystem::IsSceneDirtyThisFrame), which a MaterialInstance mutation
			// does not do. Without this, ray-traced hits keep shading the quad with the default white:
			// measured as a flat grey screen under render.pathtrace. Touching the component marks it
			// changed for one frame, and the index check above stops this repeating.
			m_World->GetRegistry().Write<MaterialComponent>(entity);
		}

		return true;
#else
		(void)material;
		return false;
#endif
	}

	void DoomSystem::Execute(Timestep)
	{
		if (!DoomEnabled.Get())
		{
			return;
		}

#ifndef SS_HAS_DOOM
		if (!m_Reported)
		{
			m_Reported = true;
			SS_WARN("doom.enabled is set but this build has no Doom: reconfigure with -DSS_ENABLE_DOOM=ON.");
		}
#else
		const auto doomView = View<DoomComponent>();
		if (doomView.begin() == doomView.end())
		{
			return; // nothing in this scene wants a Doom screen
		}

		if (!g_DoomStarted)
		{
			const std::string wad = DoomWad.Get();
			if (wad.empty() || !std::filesystem::exists(wad) || !LooksLikeWad(wad))
			{
				if (!m_Reported)
				{
					m_Reported = true;
					SS_WARN("doom.enabled is set but '{}' is not a readable IWAD/PWAD: set doom.wad to a valid "
					        "IWAD path (e.g. freedoom1.wad). Doom aborts the process on a bad IWAD, so it is "
					        "not started.",
					        wad);
				}
				return;
			}
			StartDoomThread(wad);
		}

		auto& reg = m_World->GetRegistry();
		auto& renderer = ServiceView<RendererService>();

		for (const auto entity : doomView)
		{
			const auto& doom = reg.Read<DoomComponent>(entity);
			if (!EnsureResources(entity, doom.Material))
			{
				continue;
			}

			{
				std::lock_guard lock(g_Doom->FrameMutex);
				if (g_Doom->HasFrame)
				{
					std::memcpy(m_Latest.data(), g_Doom->Frame.data(), kDoomBytes);
				}
			}

			// Uploaded even before Doom's first frame, when m_Latest is still zeros. The albedo takeover
			// happens as soon as the material resolves, which is well before the WAD finishes loading, and
			// a freshly created image has undefined contents: without this the quad samples garbage for
			// that window. m_Latest is only written here, on this thread, so it stays valid until the graph
			// records the copy later this frame.
			renderer.EnqueueTextureUpload(m_Latest.data(), kDoomBytes, m_Screen);

			// One interpreter, one screen: a second DoomComponent would upload the same frame to the same
			// texture, so stop after the first.
			break;
		}
#endif
	}
}
