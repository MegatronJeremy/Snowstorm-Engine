#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Service/Service.hpp"

#include <glm/vec3.hpp>

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

		// Sample format of a raw PCM block handed to CreateInstanceFromPcm. Named here rather than
		// exposing miniaudio's enum, for the same reason the engine type is pimpl'd away.
		enum class PcmFormat : uint8_t
		{
			U8,  // unsigned 8-bit, what Doom's DMX sound lumps are
			S16, // signed 16-bit, the usual PCM interchange format
			F32,
		};

		// Same as CreateInstance, but from PCM already in memory: a decoded lump, a generated buffer,
		// anything not on disk. The bytes are COPIED, so the caller may free them on return. The service
		// resamples to the device rate, so sampleRate is whatever the data actually is (Doom's effects are
		// around 11 kHz against a 48 kHz device).
		[[nodiscard]] InstanceId CreateInstanceFromPcm(const void* frames, uint64_t frameCount,
		                                               PcmFormat format, uint32_t channels, uint32_t sampleRate);
		void DestroyInstance(InstanceId id);

		void Play(InstanceId id);
		void Stop(InstanceId id);
		[[nodiscard]] bool IsPlaying(InstanceId id) const;

		void SetInstanceVolume(InstanceId id, float volume);
		void SetInstancePitch(InstanceId id, float pitch);
		void SetInstanceLooping(InstanceId id, bool loop);
		// Stereo placement for a NON-spatial sound: -1 hard left, 0 centre, +1 hard right. Ignored once
		// the instance is spatialised, since the listener geometry decides panning then.
		void SetInstancePan(InstanceId id, float pan);

		// --- 3D spatialisation ---
		// The ear. One listener, because the engine renders one view; miniaudio supports several and this
		// wraps index 0. Forward and up are the engine's convention (-Z forward, +Y up), which is also
		// miniaudio's default listener orientation, so no handedness conversion happens anywhere.
		void SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);

		// Off = the sound plays flat at its own volume (music, UI). On = it is positioned in the world and
		// attenuated and panned against the listener.
		void SetInstanceSpatial(InstanceId id, bool enabled);
		void SetInstancePosition(InstanceId id, const glm::vec3& position);
		// Inside minDistance the sound is at full volume; past maxDistance it stops attenuating further.
		// miniaudio's default inverse-distance model is used, which is the usual game default.
		void SetInstanceDistances(InstanceId id, float minDistance, float maxDistance);

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
