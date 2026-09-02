#pragma once

#ifdef SS_HAS_DOOM

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Snowstorm::DoomInternal
{
	// Doom's channel count (i_sound.c NUM_CHANNELS). Fixed, so the per-channel state below is an array.
	inline constexpr int kNumChannels = 16;

	struct KeyEventRecord
	{
		bool Pressed;
		unsigned char Key;
	};

	// What the sound module asks the engine to do. Doom triggers sounds from ITS thread, and
	// AudioService is main-thread-only, so requests are queued here and drained by DoomSystem instead of
	// called directly. That is not merely tidy: Doom's thread can never be joined (no shutdown entry
	// point), so a direct call would still be a use-after-free at shutdown no matter how thread-safe the
	// service was. Marshalling means the main thread simply stops draining and nothing dangles.
	struct SoundCommand
	{
		enum class Kind : uint8_t
		{
			Start,  // begin Pcm on Channel at Volume/Pan
			Stop,   // silence Channel
			Params, // retune Channel's Volume/Pan while it plays
		};

		Kind What = Kind::Stop;
		int Channel = 0;
		float Volume = 1.0f; // 0..1
		float Pan = 0.0f;    // -1 left .. +1 right

		// Decoded lump, shared rather than copied: the same effect fires repeatedly and the Doom thread
		// caches it. Only set for Start.
		std::shared_ptr<const std::vector<uint8_t>> Pcm;
		uint32_t SampleRate = 0;
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

		std::mutex SoundMutex;
		std::deque<SoundCommand> SoundCommands;

		// Read by the Doom thread (I_SoundIsPlaying) and written by the main thread as it starts and
		// reaps voices. Atomic rather than mutexed because Doom polls it constantly and only needs a
		// recent answer to decide whether to reuse a channel.
		std::array<std::atomic<bool>, kNumChannels> ChannelActive{};

		std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();

		std::string ExeArg = "snowstorm";
		std::string IwadFlag = "-iwad";
		std::string WadArg;
		std::array<char*, 3> Argv{};
	};

	// Leaked on purpose, and this is load-bearing. Doom has no shutdown entry point, so its thread is
	// detached and runs until the process dies; it is still calling DG_GetTicksMs, DG_DrawFrame and the
	// sound module while the CRT runs static destructors on the main thread. A shared_ptr or any object
	// with a destructor here would be torn down under that thread. Nothing this block owns is ever freed,
	// so there is nothing for the thread to read after its lifetime ends.
	extern DoomShared* g_Doom;
}

#endif
