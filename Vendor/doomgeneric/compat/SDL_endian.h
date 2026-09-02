// Enough of SDL_endian.h for Chocolate Doom's i_swap.h and midifile.c, which were the last reason SDL
// headers had to be reachable after the OPL work removed the SDL libraries.
//
// Between them they use six things: SDL_BYTEORDER, the two byte-order constants, and four swaps. The
// LE pair is identity on a little-endian target, but the BE pair is NOT: midifile.c applies it to the
// MIDI header fields, which are genuinely big-endian on disk, so those do real byte reversal here.
// Getting that wrong would not fail to link, it would misread every track length and time division.
//
// Functions rather than macros so an argument is evaluated exactly once, matching SDL.
//
// Reachable only from the doomgeneric target's private include path.

#ifndef SS_COMPAT_SDL_ENDIAN_H
#define SS_COMPAT_SDL_ENDIAN_H

#include <stdint.h>

#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER SDL_LIL_ENDIAN

#if defined(__BIG_ENDIAN__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#error "SS compat SDL_endian.h assumes a little-endian target; swap the LE and BE bodies before porting."
#endif

static __inline uint16_t SDL_SwapLE16(uint16_t x)
{
	return x;
}

static __inline uint32_t SDL_SwapLE32(uint32_t x)
{
	return x;
}

static __inline uint16_t SDL_SwapBE16(uint16_t x)
{
	return (uint16_t)((x << 8) | (x >> 8));
}

static __inline uint32_t SDL_SwapBE32(uint32_t x)
{
	return (x << 24) | ((x << 8) & 0x00FF0000u) | ((x >> 8) & 0x0000FF00u) | (x >> 24);
}

#endif
