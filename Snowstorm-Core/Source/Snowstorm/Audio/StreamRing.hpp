#pragma once

#include "Snowstorm/Audio/AudioService.hpp"

#include <cstdint>

namespace Snowstorm
{
	// The ring and flush rules behind AudioService's streams, with no device and no miniaudio in the
	// header. Write and WritableFrames belong to ONE producer thread, Read to the single consumer (the
	// mixer callback), RequestFlush to any thread.
	class StreamRing
	{
	public:
		StreamRing(AudioService::PcmFormat format, uint32_t channels, uint32_t capacityFrames);
		~StreamRing();
		StreamRing(const StreamRing&) = delete;
		StreamRing& operator=(const StreamRing&) = delete;

		[[nodiscard]] bool Valid() const;
		[[nodiscard]] uint32_t BytesPerFrame() const;

		// Frames actually accepted: fewer than asked when the ring is full.
		uint32_t Write(const void* frames, uint32_t frameCount);
		[[nodiscard]] uint32_t WritableFrames() const;

		// Sets one atomic; the discard runs inside the next Read.
		void RequestFlush();

		// Always fills frameCount frames: queued ones first, then silence for the underrun.
		void Read(void* out, uint64_t frameCount);

	private:
		struct Impl;
		Scope<Impl> m_Impl;
		uint32_t m_BytesPerFrame = 0;
		bool m_IsU8 = false;
	};
}
