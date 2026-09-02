// The one translation unit that compiles miniaudio itself. It is header-only, so exactly one TU in the
// program must define MINIAUDIO_IMPLEMENTATION; every other file includes the header for declarations
// only. Kept alone in its own file so its ~90k lines are compiled once and nothing else pays for them.
//
// Excluded from the precompiled header and built at /W0 in CMakeLists: it is third-party C, and its
// warnings would bury the engine's own.
//
// Only the encoder is compiled out. The other MA_NO_* switches that look tempting are not safe to guess
// at: MA_NO_RESOURCE_MANAGER_JOB_THREAD in particular makes ma_engine's async loading depend on the host
// pumping jobs itself, which is a behaviour change rather than a size saving.

#define MINIAUDIO_IMPLEMENTATION

// The engine plays audio and never writes it.
#define MA_NO_ENCODING

#include <miniaudio.h>
