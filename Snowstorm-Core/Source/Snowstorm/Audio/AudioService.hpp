#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Service/Service.hpp"

#include <cstdint>
#include <filesystem>

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

		// A playing (or ready-to-play) sound owned by this service. Opaque so the miniaudio type stays
		// out of every caller's translation unit; 0 is the null instance and every call below ignores it.
		using InstanceId = uint64_t;
		static constexpr InstanceId NullInstance = 0;

		// Decode `path` and get a stoppable, re-startable instance back. Returns NullInstance if the
		// device is unavailable or the file cannot be decoded (reported once, not per frame).
		//
		// The caller owns the lifetime and must DestroyInstance it: an emitter is expected to outlive
		// individual plays, so nothing is reclaimed automatically.
		[[nodiscard]] InstanceId CreateInstance(const std::filesystem::path& path);
		void DestroyInstance(InstanceId id);

		void Play(InstanceId id);
		void Stop(InstanceId id);
		[[nodiscard]] bool IsPlaying(InstanceId id) const;

		void SetInstanceVolume(InstanceId id, float volume);
		void SetInstancePitch(InstanceId id, float pitch);
		void SetInstanceLooping(InstanceId id, bool loop);

	private:
		// miniaudio.h is ~90k lines, so it is not dragged into every TU that touches audio: the engine
		// type lives behind this.
		struct Impl;
		Scope<Impl> m_Impl;

		float m_MasterVolume = 1.0f;

		// Monotonic, never reused. A stale id from a destroyed instance therefore misses the lookup and
		// no-ops, instead of aliasing whatever took its slot.
		InstanceId m_NextInstanceId = 1;
	};
}
