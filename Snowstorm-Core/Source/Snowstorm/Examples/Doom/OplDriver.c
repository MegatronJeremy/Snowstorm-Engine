// Doom's OPL music backend, replacing Chocolate Doom's opl.c and opl_sdl.c.
//
// Chocolate Doom's i_oplmusic.c is the sequencer: it reads GENMIDI out of the WAD, turns MIDI events
// into OPL register writes, and schedules itself forward with OPL_SetCallback. It does not produce
// audio. Producing audio is what opl_sdl.c did, and this file replaces it, for two reasons: that file
// is half SDL (it opens its own device via Mix_OpenAudio and hooks Mix_HookMusic, which is exactly the
// second output this work exists to remove), and opl.c's driver-dispatch layer drags in the Win32
// hardware-port backend and waits on an SDL condvar in OPL_Delay.
//
// So this provides the OPL_* entry points i_oplmusic.c actually calls, drives dbopl directly, and
// writes the result into an engine audio stream.
//
// TIMING is the part that has to be exact, and it is copied from opl_sdl.c's OPL_Mix_Callback rather
// than reinvented. Time advances in SAMPLES GENERATED, never by a wall clock, and each block is split
// at every pending callback deadline: generate up to the next deadline, advance time by exactly that
// many samples, fire whatever is due, repeat. Generating a whole block and then firing the callbacks
// drifts the MIDI clock, and the symptom is music that sounds subtly wrong rather than obviously
// mistimed.
//
// Plain C, and free of doomtype.h and windows.h on purpose: in C++ doomtype.h's `boolean` collides
// with the one in Windows' rpcndr.h. Threading and locking therefore live in the C++ bridge next door.

#ifdef SS_HAS_DOOM

#include "DoomAudioBridge.h"

#include "dbopl.h"
#include "opl.h"
#include "opl_queue.h"

#include <stdlib.h>
#include <string.h>

// Declared by opl_internal.h, which the real opl.c would have defined. i_oplmusic.c sets it through
// OPL_SetSampleRate before OPL_Init.
unsigned int opl_sample_rate = 48000;

static Chip s_Chip;
static int s_Initialised = 0;
static int s_Paused = 0;

static opl_callback_queue_t* s_Queue = NULL;

// Microseconds of synthesised audio produced so far. The sequencer's whole notion of "now".
static uint64_t s_CurrentTime = 0;
// Time spent paused, held out of s_CurrentTime so a pause does not make every queued callback fire at
// once on resume.
static uint64_t s_PauseOffset = 0;

// dbopl generates mono 32-bit; this is the scratch it writes into before the copy out to stereo.
static int32_t* s_MixBuffer = NULL;
static unsigned int s_MixBufferFrames = 0;

static void AdvanceTime(unsigned int nsamples)
{
	uint64_t us;

	SS_DoomAudio_LockQueue();

	us = ((uint64_t)nsamples * OPL_SECOND) / opl_sample_rate;
	s_CurrentTime += us;

	if (s_Paused)
	{
		s_PauseOffset += us;
	}

	while (!OPL_Queue_IsEmpty(s_Queue) && s_CurrentTime >= OPL_Queue_Peek(s_Queue) + s_PauseOffset)
	{
		opl_callback_t callback;
		void* callback_data;

		if (!OPL_Queue_Pop(s_Queue, &callback, &callback_data))
		{
			break;
		}

		// The queue lock must be released before invoking: the callback schedules its own successor
		// through OPL_SetCallback, which takes it again. The callback lock is what OPL_Lock holds, so
		// the game thread can keep the sequencer still while it edits state.
		SS_DoomAudio_UnlockQueue();
		SS_DoomAudio_LockCallbacks();
		callback(callback_data);
		SS_DoomAudio_UnlockCallbacks();
		SS_DoomAudio_LockQueue();
	}

	SS_DoomAudio_UnlockQueue();
}

static void FillBuffer(int16_t* buffer, unsigned int nsamples)
{
	unsigned int i;

	if (nsamples == 0)
	{
		return;
	}
	if (nsamples > s_MixBufferFrames)
	{
		nsamples = s_MixBufferFrames;
	}

	Chip__GenerateBlock2(&s_Chip, nsamples, s_MixBuffer);

	// dbopl is mono; Doom's OPL music is too. Duplicated to both channels rather than panned, matching
	// the reference backend.
	for (i = 0; i < nsamples; ++i)
	{
		buffer[i * 2] = (int16_t)s_MixBuffer[i];
		buffer[i * 2 + 1] = (int16_t)s_MixBuffer[i];
	}
}

// Produce exactly `frames` stereo frames, splitting at every callback deadline. Called by the bridge's
// producer thread; see the timing note at the top.
void SS_Opl_Generate(int16_t* buffer, unsigned int frames)
{
	unsigned int filled = 0;

	if (!s_Initialised)
	{
		memset(buffer, 0, (size_t)frames * 2 * sizeof(int16_t));
		return;
	}

	while (filled < frames)
	{
		uint64_t nsamples;

		SS_DoomAudio_LockQueue();

		if (s_Paused || OPL_Queue_IsEmpty(s_Queue))
		{
			nsamples = frames - filled;
		}
		else
		{
			const uint64_t next = OPL_Queue_Peek(s_Queue) + s_PauseOffset;
			if (next <= s_CurrentTime)
			{
				// Already due: generate nothing, let AdvanceTime(0) fire it. Bounded, because firing
				// the callback either reschedules it later or empties the queue.
				nsamples = 0;
			}
			else
			{
				nsamples = (next - s_CurrentTime) * opl_sample_rate;
				nsamples = (nsamples + OPL_SECOND - 1) / OPL_SECOND;
				if (nsamples > frames - filled)
				{
					nsamples = frames - filled;
				}
			}
		}

		SS_DoomAudio_UnlockQueue();

		FillBuffer(buffer + filled * 2, (unsigned int)nsamples);
		filled += (unsigned int)nsamples;

		AdvanceTime((unsigned int)nsamples);
	}
}

int OPL_Init(unsigned int port_base)
{
	(void)port_base; // software synth: there is no hardware port to talk to

	if (s_Initialised)
	{
		return 1;
	}

	// i_oplmusic.c passed Doom's snd_samplerate (44100) through OPL_SetSampleRate. Generating at the
	// device's rate instead costs nothing here, since every timing calculation is expressed in
	// opl_sample_rate, and it keeps the engine from resampling FM output for the whole session.
	{
		const unsigned int deviceRate = SS_DoomAudio_GetSampleRate();
		if (deviceRate != 0)
		{
			opl_sample_rate = deviceRate;
		}
	}

	s_Queue = OPL_Queue_Create();
	if (s_Queue == NULL)
	{
		return 0;
	}

	// One second of scratch, which is far more than any single generate call asks for, so FillBuffer
	// never has to loop for capacity.
	s_MixBufferFrames = opl_sample_rate;
	s_MixBuffer = (int32_t*)malloc((size_t)s_MixBufferFrames * sizeof(int32_t));
	if (s_MixBuffer == NULL)
	{
		OPL_Queue_Destroy(s_Queue);
		s_Queue = NULL;
		return 0;
	}

	DBOPL_InitTables();
	Chip__Chip(&s_Chip);
	Chip__Setup(&s_Chip, opl_sample_rate);

	s_CurrentTime = 0;
	s_PauseOffset = 0;
	s_Paused = 0;
	s_Initialised = 1;

	// The bridge starts pulling only once the synth can answer, so this is the last step.
	SS_DoomAudio_StartMusic(opl_sample_rate);
	return 1;
}

void OPL_Shutdown(void)
{
	if (!s_Initialised)
	{
		return;
	}

	// Stopped before anything is freed: the producer thread calls SS_Opl_Generate, which reads all of it.
	SS_DoomAudio_StopMusic();

	s_Initialised = 0;

	OPL_Queue_Destroy(s_Queue);
	s_Queue = NULL;

	free(s_MixBuffer);
	s_MixBuffer = NULL;
	s_MixBufferFrames = 0;
}

void OPL_SetSampleRate(unsigned int rate)
{
	if (rate != 0)
	{
		opl_sample_rate = rate;
	}
}

void OPL_WriteRegister(int reg, int value)
{
	// Timer registers are not emulated: i_oplmusic.c never reads the status port, so nothing can
	// observe them, and the reference backend only maintained them for hardware detection.
	if (s_Initialised)
	{
		Chip__WriteReg(&s_Chip, (Bit32u)reg, (Bit8u)value);
	}
}

void OPL_SetCallback(uint64_t us, opl_callback_t callback, void* data)
{
	SS_DoomAudio_LockQueue();
	OPL_Queue_Push(s_Queue, callback, data, s_CurrentTime - s_PauseOffset + us);
	SS_DoomAudio_UnlockQueue();
}

void OPL_ClearCallbacks(void)
{
	SS_DoomAudio_LockQueue();
	OPL_Queue_Clear(s_Queue);
	SS_DoomAudio_UnlockQueue();
}

void OPL_AdjustCallbacks(float factor)
{
	SS_DoomAudio_LockQueue();
	OPL_Queue_AdjustCallbacks(s_Queue, s_CurrentTime, factor);
	SS_DoomAudio_UnlockQueue();
}

void OPL_Lock(void)
{
	SS_DoomAudio_LockCallbacks();
}

void OPL_Unlock(void)
{
	SS_DoomAudio_UnlockCallbacks();
}

void OPL_SetPaused(int paused)
{
	s_Paused = paused;
}

#endif
