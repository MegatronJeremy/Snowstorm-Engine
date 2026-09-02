// doomgeneric's i_sound.c has a bare #include <SDL_mixer.h> at the top, but every remaining mention of
// SDL_mixer in that file is in a COMMENT: nothing calls Mix_*. The include is vestigial from the SDL
// backends, which the OPL work removed.
//
// An empty header satisfies it without keeping SDL as a dependency. If a real Mix_* call ever appears,
// it will fail to compile here rather than silently pulling SDL back in.

#ifndef SS_COMPAT_SDL_MIXER_H
#define SS_COMPAT_SDL_MIXER_H
#endif
