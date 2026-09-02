// The C++ half of Doom's OPL music path: locking, the producer thread, and the engine audio stream.
// See DoomAudioBridge.h for why this is split from the driver.

#ifdef SS_HAS_DOOM

#include "DoomAudioBridge.h"

#include "DoomShared.hpp"

#include "Snowstorm/Audio/AudioService.hpp"
#include "Snowstorm/Core/Log.hpp"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
	using Snowstorm::AudioService;

	std::mutex g_QueueMutex;

	// Recursive because the game thread can be inside OPL_Lock when it calls a driver entry point that
	// locks again; the reference backend relies on the same re-entrancy.
	std::recursive_mutex g_CallbackMutex;

	std::atomic<bool> g_Running{false};
	std::thread g_Producer;

	// How much audio to keep queued ahead of the mixer. 8192 frames at 48 kHz is ~170 ms, which is
	// enough to ride out a frame hitch or a Doom tic that runs long, and short enough that a music
	// change is not audibly late.
	constexpr uint32_t kStreamCapacityFrames = 8192;

	// Generated per wake-up at most. Small enough that the sub-block splitting inside the driver stays
	// fine-grained, large enough not to spin.
	constexpr uint32_t kChunkFrames = 1024;

	void ProducerMain(AudioService* audio, AudioService::StreamHandle* stream)
	{
		std::vector<int16_t> scratch(static_cast<size_t>(kChunkFrames) * 2);

		while (g_Running.load(std::memory_order_relaxed))
		{
			const uint32_t writable = audio->StreamWritableFrames(stream);
			if (writable < kChunkFrames)
			{
				// The ring is full enough. Sleeping a fraction of the buffer depth keeps this thread
				// idle without letting the ring drain while it is asleep.
				std::this_thread::sleep_for(std::chrono::milliseconds(4));
				continue;
			}

			const uint32_t frames = writable < kChunkFrames ? writable : kChunkFrames;
			SS_Opl_Generate(scratch.data(), frames);
			audio->StreamWrite(stream, scratch.data(), frames);
		}
	}
}

extern "C"
{
	void SS_DoomAudio_LockQueue(void)
	{
		g_QueueMutex.lock();
	}

	void SS_DoomAudio_UnlockQueue(void)
	{
		g_QueueMutex.unlock();
	}

	void SS_DoomAudio_LockCallbacks(void)
	{
		g_CallbackMutex.lock();
	}

	void SS_DoomAudio_UnlockCallbacks(void)
	{
		g_CallbackMutex.unlock();
	}

	unsigned int SS_DoomAudio_GetSampleRate(void)
	{
		using namespace Snowstorm::DoomInternal;

		if (g_Doom == nullptr)
		{
			return 0;
		}
		const auto* audio = static_cast<const AudioService*>(g_Doom->Audio);
		return audio != nullptr ? audio->GetSampleRate() : 0;
	}

	void SS_DoomAudio_StartMusic(const unsigned int sampleRate)
	{
		using namespace Snowstorm::DoomInternal;

		if (g_Running.load(std::memory_order_relaxed) || g_Doom == nullptr)
		{
			return;
		}

		// Both were captured on the main thread before Doom started: AudioService's instance table is
		// main-thread-only, so this thread may only use the stream handle it was handed.
		auto* audio = static_cast<AudioService*>(g_Doom->Audio);
		auto* stream = static_cast<AudioService::StreamHandle*>(g_Doom->MusicStream);
		if (audio == nullptr || stream == nullptr)
		{
			SS_CORE_WARN("Doom music: no audio stream was created; music will be silent.");
			return;
		}

		if (sampleRate != audio->GetSampleRate())
		{
			// Not fatal (the engine resamples), but it means the synth is doing work at one rate and the
			// device another, which is pure loss for a generator that could produce at the target.
			SS_CORE_WARN("Doom music: OPL is generating at {} Hz against a {} Hz device.",
			             sampleRate, audio->GetSampleRate());
		}

		g_Running.store(true, std::memory_order_relaxed);
		g_Producer = std::thread(ProducerMain, audio, stream);
	}

	void SS_DoomAudio_StopMusic(void)
	{
		if (!g_Running.exchange(false, std::memory_order_relaxed))
		{
			return;
		}
		// Joined, unlike Doom's own thread: this one the engine created and can therefore end, and it
		// must be gone before the driver frees the state SS_Opl_Generate reads.
		if (g_Producer.joinable())
		{
			g_Producer.join();
		}
	}
}

#endif
