// Doom's sound-effect backend, implemented against the engine's audio system instead of SDL_mixer.
//
// Everything here runs on the DOOM thread. It never touches AudioService: it decodes lumps and posts
// requests into the shared block, which DoomSystem drains on the main thread. See SoundCommand in
// DoomShared.hpp for why that indirection is not optional.

#ifdef SS_HAS_DOOM

#include "DoomShared.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

// Standard headers first so their guards are set before the C headers below borrow them.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// This TU skips the precompiled header (see Snowstorm-Core/CMakeLists.txt). It has to: in C++
// doomtype.h makes `boolean` an unsigned int, Windows' rpcndr.h makes it an unsigned char, and pch.h
// force-includes Windows.h into every other Core source.
extern "C"
{
#include <i_sound.h>
#include <m_misc.h>
#include <w_wad.h>
#include <z_zone.h>
}

namespace
{
	using namespace Snowstorm::DoomInternal;

	// Decoded lumps, keyed by lump number. Doom re-triggers the same effect constantly, and a shared_ptr
	// through the queue means a repeat costs a pointer copy rather than another decode. Doom-thread only.
	std::unordered_map<int, std::pair<std::shared_ptr<const std::vector<uint8_t>>, uint32_t>> g_Cache;

	// Decode a DMX sound lump. Layout (see the reference SDL backend): u16 magic 0x0003, u16 sample rate,
	// u32 length, then unsigned 8-bit mono PCM. DMX also pads 16 bytes at each end, which is why the data
	// starts 16 in and the length loses 32; lumps of 48 samples or fewer are what vanilla treats as
	// invalid. Returns false and leaves the outputs alone on anything malformed.
	bool DecodeSfx(const int lumpNum, std::shared_ptr<const std::vector<uint8_t>>& outPcm, uint32_t& outRate)
	{
		if (const auto it = g_Cache.find(lumpNum); it != g_Cache.end())
		{
			outPcm = it->second.first;
			outRate = it->second.second;
			return outPcm != nullptr;
		}

		const auto* data = static_cast<const uint8_t*>(W_CacheLumpNum(lumpNum, PU_STATIC));
		const unsigned int lumpLen = W_LumpLength(lumpNum);

		std::shared_ptr<const std::vector<uint8_t>> pcm;
		uint32_t rate = 0;

		if (data != nullptr && lumpLen >= 8 && data[0] == 0x03 && data[1] == 0x00)
		{
			rate = static_cast<uint32_t>(data[3] << 8 | data[2]);
			unsigned int length = static_cast<unsigned int>(data[7]) << 24 | static_cast<unsigned int>(data[6]) << 16 |
			                      static_cast<unsigned int>(data[5]) << 8 | static_cast<unsigned int>(data[4]);

			if (length <= lumpLen - 8 && length > 48 && rate > 0)
			{
				length -= 32;
				pcm = std::make_shared<const std::vector<uint8_t>>(data + 24, data + 24 + length);
			}
		}

		// Cached even on failure, so a broken lump is parsed once rather than on every trigger.
		g_Cache.emplace(lumpNum, std::make_pair(pcm, rate));
		outPcm = pcm;
		outRate = rate;
		return pcm != nullptr;
	}

	void Post(SoundCommand&& command)
	{
		if (g_Doom == nullptr)
		{
			return;
		}
		std::lock_guard lock(g_Doom->SoundMutex);
		// The main thread drains every frame, so this only backs up if rendering has stalled. Dropping the
		// oldest bounds it; a stale trigger is less bad than unbounded growth.
		constexpr size_t maxQueued = 256;
		if (g_Doom->SoundCommands.size() >= maxQueued)
		{
			g_Doom->SoundCommands.pop_front();
		}
		g_Doom->SoundCommands.push_back(std::move(command));
	}

	// Doom's volume is 0..127 and its stereo separation 0..254 with 128 centre.
	float ToVolume(const int vol)
	{
		return static_cast<float>(vol) / 127.0f;
	}
	float ToPan(const int sep)
	{
		return (static_cast<float>(sep) - 127.0f) / 127.0f;
	}

	boolean SS_InitSound(boolean)
	{
		return true;
	}

	void SS_ShutdownSound()
	{
	}

	int SS_GetSfxLumpNum(sfxinfo_t* sfx)
	{
		char name[9];
		M_snprintf(name, sizeof(name), "ds%s", sfx->name);
		return W_GetNumForName(name);
	}

	void SS_UpdateSound()
	{
		// Voice reaping is the main thread's job (it owns the instances), so there is nothing to do per
		// tic here. Kept because the module interface requires the pointer.
	}

	void SS_UpdateSoundParams(const int channel, const int vol, const int sep)
	{
		if (channel < 0 || channel >= kNumChannels)
		{
			return;
		}
		SoundCommand cmd;
		cmd.What = SoundCommand::Kind::Params;
		cmd.Channel = channel;
		cmd.Volume = ToVolume(vol);
		cmd.Pan = ToPan(sep);
		Post(std::move(cmd));
	}

	int SS_StartSound(sfxinfo_t* sfxinfo, const int channel, const int vol, const int sep)
	{
		if (sfxinfo == nullptr || channel < 0 || channel >= kNumChannels)
		{
			return -1;
		}

		SoundCommand cmd;
		if (!DecodeSfx(sfxinfo->lumpnum, cmd.Pcm, cmd.SampleRate))
		{
			return -1;
		}

		cmd.What = SoundCommand::Kind::Start;
		cmd.Channel = channel;
		cmd.Volume = ToVolume(vol);
		cmd.Pan = ToPan(sep);

		// Marked active immediately rather than when the main thread starts it: Doom asks
		// SoundIsPlaying right after, and answering "no" would let it reuse the channel at once.
		if (g_Doom != nullptr)
		{
			g_Doom->ChannelActive[channel].store(true, std::memory_order_relaxed);
		}
		Post(std::move(cmd));
		return channel;
	}

	void SS_StopSound(const int channel)
	{
		if (channel < 0 || channel >= kNumChannels)
		{
			return;
		}
		if (g_Doom != nullptr)
		{
			g_Doom->ChannelActive[channel].store(false, std::memory_order_relaxed);
		}
		SoundCommand cmd;
		cmd.What = SoundCommand::Kind::Stop;
		cmd.Channel = channel;
		Post(std::move(cmd));
	}

	boolean SS_SoundIsPlaying(const int channel)
	{
		if (channel < 0 || channel >= kNumChannels || g_Doom == nullptr)
		{
			return false;
		}
		return g_Doom->ChannelActive[channel].load(std::memory_order_relaxed) ? true : false;
	}

	void SS_CacheSounds(sfxinfo_t*, int)
	{
		// Lumps are decoded on first use and cached from then on, so precaching would only move the same
		// work earlier while holding every effect in memory whether the level uses it or not.
	}

	// Doom picks a sound backend by matching snd_sfxdevice against this list; SNDDEVICE_SB is the
	// default in i_sound.c, so it has to be here or no module is selected and the game is silent.
	snddevice_t g_SoundDevices[] = {
	    SNDDEVICE_SB,
	    SNDDEVICE_PAS,
	    SNDDEVICE_GUS,
	    SNDDEVICE_WAVEBLASTER,
	    SNDDEVICE_SOUNDCANVAS,
	    SNDDEVICE_AWE32,
	};
}

extern "C"
{
	// i_sound.c binds these as config variables (i_sound.c:397-398, 412-413) and the SDL backend used to
	// define them. It is gone, so this backend owns them. Both are inert here: libsamplerate resampling
	// was SDL's way of matching the mixer rate, and miniaudio resamples each buffer to the device rate
	// itself. Defined only so I_BindSoundVariables still links, with the values upstream defaulted to.
	int use_libsamplerate = 0;
	float libsamplerate_scale = 0.65f;

	sound_module_t DG_sound_module = {
	    g_SoundDevices,
	    static_cast<int>(sizeof(g_SoundDevices) / sizeof(*g_SoundDevices)),
	    SS_InitSound,
	    SS_ShutdownSound,
	    SS_GetSfxLumpNum,
	    SS_UpdateSound,
	    SS_UpdateSoundParams,
	    SS_StartSound,
	    SS_StopSound,
	    SS_SoundIsPlaying,
	    SS_CacheSounds,
	};
}

#endif
