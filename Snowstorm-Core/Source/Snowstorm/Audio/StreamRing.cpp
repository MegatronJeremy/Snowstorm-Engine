#include "StreamRing.hpp"

#include <miniaudio.h>

#include <atomic>
#include <cstring>

namespace Snowstorm
{
	namespace
	{
		ma_format ToMaFormat(const AudioService::PcmFormat format)
		{
			switch (format)
			{
			case AudioService::PcmFormat::U8:
				return ma_format_u8;
			case AudioService::PcmFormat::S16:
				return ma_format_s16;
			case AudioService::PcmFormat::F32:
				return ma_format_f32;
			}
			return ma_format_u8;
		}

		uint32_t BytesPerSampleFor(const AudioService::PcmFormat format)
		{
			switch (format)
			{
			case AudioService::PcmFormat::U8:
				return 1;
			case AudioService::PcmFormat::S16:
				return 2;
			case AudioService::PcmFormat::F32:
				return 4;
			}
			return 1;
		}
	}

	struct StreamRing::Impl
	{
		ma_pcm_rb Ring{};
		bool Ready = false;

		// Set by RequestFlush on any thread, consumed by Read. The only cross-thread state here, and
		// deliberately the only thing RequestFlush touches.
		std::atomic<bool> FlushRequested{false};
	};

	StreamRing::StreamRing(const AudioService::PcmFormat format, const uint32_t channels, const uint32_t capacityFrames)
	    : m_Impl(CreateScope<Impl>())
	{
		m_BytesPerFrame = BytesPerSampleFor(format) * channels;
		m_IsU8 = format == AudioService::PcmFormat::U8;
		if (channels == 0 || capacityFrames == 0)
		{
			return;
		}
		m_Impl->Ready =
		    ma_pcm_rb_init(ToMaFormat(format), channels, capacityFrames, nullptr, nullptr, &m_Impl->Ring) == MA_SUCCESS;
	}

	StreamRing::~StreamRing()
	{
		if (m_Impl->Ready)
		{
			ma_pcm_rb_uninit(&m_Impl->Ring);
		}
	}

	bool StreamRing::Valid() const
	{
		return m_Impl->Ready;
	}

	uint32_t StreamRing::BytesPerFrame() const
	{
		return m_BytesPerFrame;
	}

	uint32_t StreamRing::Write(const void* frames, const uint32_t frameCount)
	{
		if (!m_Impl->Ready || frames == nullptr || frameCount == 0)
		{
			return 0;
		}

		const auto* in = static_cast<const uint8_t*>(frames);
		uint32_t done = 0;
		while (done < frameCount)
		{
			ma_uint32 want = frameCount - done;
			void* dst = nullptr;
			if (ma_pcm_rb_acquire_write(&m_Impl->Ring, &want, &dst) != MA_SUCCESS || want == 0)
			{
				break; // full; the caller keeps the rest
			}
			std::memcpy(dst, in + static_cast<size_t>(done) * m_BytesPerFrame, static_cast<size_t>(want) * m_BytesPerFrame);
			ma_pcm_rb_commit_write(&m_Impl->Ring, want);
			done += want;
		}
		return done;
	}

	uint32_t StreamRing::WritableFrames() const
	{
		return m_Impl->Ready ? ma_pcm_rb_available_write(&m_Impl->Ring) : 0;
	}

	void StreamRing::RequestFlush()
	{
		m_Impl->FlushRequested.store(true, std::memory_order_release);
	}

	void StreamRing::Read(void* framesOut, const uint64_t frameCount)
	{
		auto* out = static_cast<uint8_t*>(framesOut);
		uint64_t done = 0;

		if (m_Impl->Ready)
		{
			// The flush happens HERE, on the consumer, rather than in RequestFlush on the caller's thread.
			// ma_pcm_rb's cursors are two words updated by load-compute-exchange with no CAS, so the only
			// thread that may move the read cursor is the one that reads. Calling ma_pcm_rb_reset or
			// seek_read from elsewhere does not merely lose a flush: it can leave the write cursor an
			// arbitrary distance ahead of the read cursor, and the reader then treats whatever lies between
			// as valid PCM.
			//
			// Before the drain, not after, or this callback emits one last block it has already decided to
			// discard. Seeded from available_read because acquire_read converts frames to bytes in 32-bit
			// arithmetic, so a large sentinel count overflows.
			if (m_Impl->FlushRequested.exchange(false, std::memory_order_acquire))
			{
				for (ma_uint32 avail = ma_pcm_rb_available_read(&m_Impl->Ring); avail > 0;
				     avail = ma_pcm_rb_available_read(&m_Impl->Ring))
				{
					void* src = nullptr;
					if (ma_pcm_rb_acquire_read(&m_Impl->Ring, &avail, &src) != MA_SUCCESS || avail == 0)
					{
						break;
					}
					ma_pcm_rb_commit_read(&m_Impl->Ring, avail);
				}
			}

			// Drained in as many passes as the ring's wrap needs, then padded.
			while (done < frameCount)
			{
				ma_uint32 want = static_cast<ma_uint32>(frameCount - done);
				void* src = nullptr;
				if (ma_pcm_rb_acquire_read(&m_Impl->Ring, &want, &src) != MA_SUCCESS || want == 0)
				{
					break;
				}
				std::memcpy(out + done * m_BytesPerFrame, src, static_cast<size_t>(want) * m_BytesPerFrame);
				ma_pcm_rb_commit_read(&m_Impl->Ring, want);
				done += want;
			}
		}

		if (done < frameCount)
		{
			// Underrun. Silence is format-dependent: unsigned 8-bit is centred at 128, the rest at 0.
			std::memset(out + done * m_BytesPerFrame, m_IsU8 ? 0x80 : 0,
			            static_cast<size_t>(frameCount - done) * m_BytesPerFrame);
		}
	}
}
