// C entry points the OPL driver (OplDriver.c) uses to reach the engine.
//
// The driver is deliberately plain C and free of windows.h, because doomtype.h's `boolean` collides
// with the one in Windows' rpcndr.h. Threads, mutexes and AudioService all live on the C++ side of
// this header instead.

#ifndef SS_DOOM_AUDIO_BRIDGE_H
#define SS_DOOM_AUDIO_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

	// Guards the callback queue. Held only for the short edits around it, never across a callback.
	void SS_DoomAudio_LockQueue(void);
	void SS_DoomAudio_UnlockQueue(void);

	// Held while a sequencer callback runs. This is what OPL_Lock exposes to the game thread, so it can
	// hold the sequencer still while mutating the state a callback would otherwise touch. Recursive,
	// because the game thread can already hold it when it calls back into the driver.
	void SS_DoomAudio_LockCallbacks(void);
	void SS_DoomAudio_UnlockCallbacks(void);

	// The output device's sample rate, or 0 if audio is unavailable. The synth generates at this rate
	// rather than at Doom's configured snd_samplerate, so nothing has to resample.
	unsigned int SS_DoomAudio_GetSampleRate(void);

	// Begin and end pulling audio out of the synth. Between these, a producer thread calls
	// SS_Opl_Generate to fill the engine's music stream.
	void SS_DoomAudio_StartMusic(unsigned int sampleRate);
	void SS_DoomAudio_StopMusic(void);

	// Stop, and latch music off permanently. The engine calls this as it tears the stream down, so a
	// later OPL_Init on Doom's thread cannot restart a producer that would write into freed memory.
	void SS_DoomAudio_ShutdownMusic(void);

	// Implemented by the driver, called by the producer thread: exactly `frames` stereo 16-bit frames.
	void SS_Opl_Generate(int16_t* buffer, unsigned int frames);

#ifdef __cplusplus
}
#endif

#endif
