# Audio

## Audio

`AudioService` (miniaudio) owns the device and the mixer; `AudioSystem` turns `AudioSourceComponent`
into playing voices and pushes the `AudioListenerComponent` pose. miniaudio was chosen over OpenAL Soft
on licence: it is `Unlicense OR MIT-0`, OpenAL Soft is LGPL, and this repo is public domain.

**The service is main-thread-only, with exactly two documented exceptions**, both on the same grounds:
they touch a stream's own lock-free ring or a single atomic inside its handle, never the instance table.
`StreamWrite`/`StreamWritableFrames` are callable from ONE producer thread, and `StreamFlush` from any
thread. Everything else (create, destroy, play, the per-instance setters) is main thread. Doom is what
forced the rule: it triggers sounds from its own unjoinable thread, so its effects are marshalled through
a command queue instead of calling in.

**A stream flush runs on the consumer, not the caller.** `ma_pcm_rb`'s two cursors are updated by
load-compute-exchange with no CAS, so only the thread that reads may move the read cursor. `StreamFlush`
therefore just sets an atomic and the mixer callback does the discard on its next pull. Calling
`ma_pcm_rb_reset` or `seek_read` from a game thread does not merely lose a flush: it can leave the write
cursor an arbitrary distance ahead of the read cursor, and the reader treats what lies between as PCM.

The ring, flush and underrun rules live in `Audio/StreamRing` (miniaudio hidden behind a pimpl), which needs no device; `StreamRingTests` pin them GPU-free and device-free. `AudioService` stream calls forward to it.

**Pan law is per instance and defaults to miniaudio's balance.** `PanLaw::Balance` attenuates only the
channel you pan away from, so a centred sound is 6 dB louder than a hard-panned one; `PanLaw::ConstantSum`
moves energy across and keeps L+R fixed, which is what a mono point source needs and what Doom's own
backend does. ConstantSum reaches a gain of 2 at hard pan, the only place a single sound exceeds its own
volume setting, so a caller opting in leaves the headroom (Doom does, by scaling its 0..127 volume by
1/255 rather than 1/127, which reproduces the SDL_mixer gains exactly). Unreal makes this a project
setting and miniaudio makes it per sound; per instance follows the backend and lets Doom's voices differ
from a future UI or music source.

**Starting a voice at runtime is a request on the component.** `PlayRequested`/`StopRequested` are set
by the editor's transport buttons or by a script and consumed by `AudioSystem` the same frame. They are
deliberately absent from `AudioSourceComponent.cpp`'s RTTR registration, and that omission is the whole
mechanism keeping them transient: that one property list is simultaneously the serialization set and the
inspector set, so registering them to get a checkbox would write `"PlayRequested": true` into the
`.world` file and re-trigger the sound on every load.

The per-source decision (play, stop, latch, drop-and-clear with no instance) and the sweep predicate are the pure `DecideAudioSource`/`KeepAudioVoice` in `Systems/AudioSourceRule.hpp`, tested by `AudioSourceRuleTests`. Stop runs before Play and latches `Started`, so `PlayOnStart` never undoes a stop.

Voices are owned by `AudioSystem`, never by the entity: entity deletion, scene load and the editor's
Play/Stop snapshot restore all destroy components without notice, so a per-entity id would strand a
playing sound nothing can reach. `Execute` sweeps voices whose entity is gone.
