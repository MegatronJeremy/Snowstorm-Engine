#include "AudioService.hpp"

#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/Log.hpp"

#include <miniaudio.h>

namespace Snowstorm
{
	struct AudioService::Impl
	{
		ma_engine Engine{};
	};

	AudioService::AudioService()
	{
		if (!CVars::AudioEnabled.Get())
		{
			SS_CORE_INFO("Audio: disabled (audio.enabled=0); no output device opened.");
			return;
		}

		auto impl = CreateScope<Impl>();

		// Null config = miniaudio picks the default playback device and its native sample rate, which is
		// what a game should do: resampling to a hardcoded rate costs quality and latency for nothing.
		if (const ma_result result = ma_engine_init(nullptr, &impl->Engine); result != MA_SUCCESS)
		{
			// Not an error: a machine with no sound card, or one whose device is exclusively held by
			// something else, must still run. Every call below no-ops from here.
			SS_CORE_WARN("Audio: no output device ({}); running silently.", ma_result_description(result));
			return;
		}

		m_Impl = std::move(impl);

		m_MasterVolume = CVars::AudioMasterVolume.Get();
		ma_engine_set_volume(&m_Impl->Engine, m_MasterVolume);

		const ma_device* device = ma_engine_get_device(&m_Impl->Engine);
		SS_CORE_INFO("Audio: '{}' at {} Hz, {} channels.",
		             device != nullptr ? device->playback.name : "<unknown device>",
		             ma_engine_get_sample_rate(&m_Impl->Engine),
		             ma_engine_get_channels(&m_Impl->Engine));
	}

	AudioService::~AudioService()
	{
		if (m_Impl)
		{
			// Stops the callback thread and joins it before the mixing graph goes away, so nothing is
			// still reading a sound while it is destroyed.
			ma_engine_uninit(&m_Impl->Engine);
		}
	}

	void AudioService::OnUpdate(Timestep)
	{
		if (const float cvarVolume = CVars::AudioMasterVolume.Get(); cvarVolume != m_MasterVolume)
		{
			SetMasterVolume(cvarVolume);
		}
	}

	void AudioService::SetMasterVolume(const float volume)
	{
		m_MasterVolume = volume;
		if (m_Impl)
		{
			ma_engine_set_volume(&m_Impl->Engine, volume);
		}
	}
}
