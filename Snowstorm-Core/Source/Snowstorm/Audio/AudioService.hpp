#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Service/Service.hpp"

namespace Snowstorm
{
	// Application-scoped audio subsystem: owns the output device and the mixing graph, shared across every
	// World, like RendererService is for the GPU. Cf. Unity's AudioSettings/AudioMixer or Unreal's
	// FAudioDevice, both of which are engine-scoped with per-scene emitters on top.
	//
	// The device is a real piece of hardware that can be absent, in use, or fail to open, so an unavailable
	// service is a supported state rather than a fatal one: IsAvailable() reports it, every call is a no-op
	// while it holds, and the engine runs silently. A machine with no sound card must still boot.
	//
	// miniaudio runs its own callback thread and does the mixing there. Nothing in this class may be called
	// from that thread; the public API here is for the main thread only.
	class AudioService final : public Service
	{
	public:
		AudioService();
		~AudioService() override;

		// Picks up a live edit to audio.volume (the editor's CVar panel), matching how the rendering CVars
		// behave: change it and hear it, without a relaunch.
		void OnUpdate(Timestep ts) override;

		// False when the output device could not be opened (no device, driver failure, audio.enabled off).
		// Callers do not need to check it: every method below is a no-op in that state.
		[[nodiscard]] bool IsAvailable() const { return m_Impl != nullptr; }

		// 0 = silence, 1 = unattenuated. Applied to the whole mix, so it is the engine's master volume
		// rather than a per-sound one. Values above 1 amplify and can clip; not clamped for that reason.
		void SetMasterVolume(float volume);
		[[nodiscard]] float GetMasterVolume() const { return m_MasterVolume; }

	private:
		// miniaudio.h is ~90k lines, so it is not dragged into every TU that touches audio: the engine
		// type lives behind this.
		struct Impl;
		Scope<Impl> m_Impl;

		float m_MasterVolume = 1.0f;
	};
}
