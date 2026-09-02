#include "AudioService.hpp"

#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/Log.hpp"

#include <miniaudio.h>

#include <string>
#include <unordered_map>

namespace Snowstorm
{
	struct AudioService::Impl
	{
		ma_engine Engine{};

		// ma_sound is not movable (the engine holds pointers into it once started), so each one is owned
		// through a stable heap allocation rather than living inside the map's value.
		std::unordered_map<InstanceId, Scope<ma_sound>> Sounds;

		// Lives on Impl rather than being a private member of AudioService so it can return ma_sound*
		// without that type appearing in the header.
		[[nodiscard]] ma_sound* Find(const InstanceId id) const
		{
			const auto it = Sounds.find(id);
			return it != Sounds.end() ? it->second.get() : nullptr;
		}
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
		if (!m_Impl)
		{
			return;
		}

		// Order matters: every sound must be torn down before the engine that mixes it. ma_engine_uninit
		// stops and joins the callback thread, so doing it the other way round would leave that thread
		// walking sounds as they are freed.
		for (auto& [id, sound] : m_Impl->Sounds)
		{
			ma_sound_uninit(sound.get());
		}
		m_Impl->Sounds.clear();

		ma_engine_uninit(&m_Impl->Engine);
	}

	AudioService::InstanceId AudioService::CreateInstance(const std::filesystem::path& path)
	{
		if (!m_Impl)
		{
			return NullInstance;
		}

		auto sound = CreateScope<ma_sound>();

		// MA_SOUND_FLAG_DECODE decodes up front rather than streaming: a sound effect is small and
		// re-triggered often, and decoding on the audio thread is what causes dropouts. Long music beds
		// are the case for streaming, which is a flag change here when something needs it.
		const std::string pathStr = path.string();
		if (const ma_result result = ma_sound_init_from_file(&m_Impl->Engine, pathStr.c_str(),
		                                                     MA_SOUND_FLAG_DECODE, nullptr, nullptr, sound.get());
		    result != MA_SUCCESS)
		{
			SS_CORE_ERROR("Audio: cannot load '{}' ({})", pathStr, ma_result_description(result));
			return NullInstance;
		}

		const InstanceId id = m_NextInstanceId++;
		m_Impl->Sounds.emplace(id, std::move(sound));
		return id;
	}

	void AudioService::DestroyInstance(const InstanceId id)
	{
		if (!m_Impl)
		{
			return;
		}
		if (const auto it = m_Impl->Sounds.find(id); it != m_Impl->Sounds.end())
		{
			ma_sound_uninit(it->second.get());
			m_Impl->Sounds.erase(it);
		}
	}

	void AudioService::Play(const InstanceId id)
	{
		if (ma_sound* sound = m_Impl ? m_Impl->Find(id) : nullptr)
		{
			// Restart from the beginning rather than resuming: a re-trigger is what a caller asking to
			// play a stopped one-shot means. ma_sound_start alone would continue from where Stop left off.
			ma_sound_seek_to_pcm_frame(sound, 0);
			ma_sound_start(sound);
		}
	}

	void AudioService::Stop(const InstanceId id)
	{
		if (ma_sound* sound = m_Impl ? m_Impl->Find(id) : nullptr)
		{
			ma_sound_stop(sound);
		}
	}

	bool AudioService::IsPlaying(const InstanceId id) const
	{
		const ma_sound* sound = m_Impl ? m_Impl->Find(id) : nullptr;
		return sound != nullptr && ma_sound_is_playing(sound) == MA_TRUE;
	}

	void AudioService::SetInstanceVolume(const InstanceId id, const float volume)
	{
		if (ma_sound* sound = m_Impl ? m_Impl->Find(id) : nullptr)
		{
			ma_sound_set_volume(sound, volume);
		}
	}

	void AudioService::SetInstancePitch(const InstanceId id, const float pitch)
	{
		if (ma_sound* sound = m_Impl ? m_Impl->Find(id) : nullptr)
		{
			ma_sound_set_pitch(sound, pitch);
		}
	}

	void AudioService::SetInstanceLooping(const InstanceId id, const bool loop)
	{
		if (ma_sound* sound = m_Impl ? m_Impl->Find(id) : nullptr)
		{
			ma_sound_set_looping(sound, loop ? MA_TRUE : MA_FALSE);
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
