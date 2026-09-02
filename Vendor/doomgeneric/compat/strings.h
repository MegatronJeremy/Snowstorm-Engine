// MSVC has no POSIX <strings.h>.
//
// doomgeneric ships config.h as a checked-in autoconf output from a Unix run, so it defines
// HAVE_STRINGS_H unconditionally. SDL's public SDL_stdinc.h honours that macro, and the two SDL audio
// backends (i_sdlsound.c, i_sdlmusic.c) include config.h before <SDL.h>, so the include fires on a
// build upstream never does: its SDL target is a Unix makefile.
//
// Nothing is declared here on purpose. Everything <strings.h> would provide that doomgeneric actually
// uses is already handled: doomtype.h maps strcasecmp/strncasecmp to the CRT's _stricmp/_strnicmp on
// Windows, and redefining them here would collide with that.
//
// Reachable only by the doomgeneric target (a PRIVATE include directory), so it cannot shadow a real
// <strings.h> for engine code on another platform.

#pragma once

#include <string.h>
