#include "AudioService.hpp"

#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/Log.hpp"

#include <miniaudio.h>

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace Snowstorm
{
	struct AudioService::Impl
	{
		ma_engine Engine{};

		// A sound, plus (for the in-memory case) the buffer it reads from. ma_sound is not movable once
		// started and ma_audio_buffer holds a pointer into Pcm, so all three are owned together through
		// stable heap allocations rather than as map values that could move or fall out of step.
		struct Entry
		{
			Scope<ma_sound> Sound;
			Scope<ma_audio_buffer> Buffer; // null for file-backed sounds
			std::vector<uint8_t> Pcm;      // the copy Buffer points into; empty for file-backed sounds
		};
		std::unordered_map<InstanceId, Entry> Sounds;

		// Lives on Impl rather than being a private member of AudioService so it can return ma_sound*
		// without that type appearing in the header.
		[[nodiscard]] ma_sound* Find(const InstanceId id) const
		{
			const auto it = Sounds.find(id);
			return it != Sounds.end() ? it->second.Sound.get() : nullptr;
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
		for (auto& [id, entry] : m_Impl->Sounds)
		{
			ma_sound_uninit(entry.Sound.get());
			if (entry.Buffer)
			{
				ma_audio_buffer_uninit(entry.Buffer.get());
			}
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
		m_Impl->Sounds.emplace(id, Impl::Entry{std::move(sound), nullptr, {}});
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
			// Sound before buffer: the sound reads from the buffer, so tearing the buffer down first
			// would leave a live reader pointing at freed memory.
			ma_sound_uninit(it->second.Sound.get());
			if (it->second.Buffer)
			{
				ma_audio_buffer_uninit(it->second.Buffer.get());
			}
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

	AudioService::InstanceId AudioService::CreateInstanceFromPcm(const void* frames, const uint64_t frameCount,
	                                                             const PcmFormat format, const uint32_t channels,
	                                                             const uint32_t sampleRate)
	{
		if (!m_Impl || frames == nullptr || frameCount == 0 || channels == 0 || sampleRate == 0)
		{
			return NullInstance;
		}

		ma_format maFormat = ma_format_u8;
		uint32_t bytesPerSample = 1;
		switch (format)
		{
		case PcmFormat::U8:
			maFormat = ma_format_u8;
			bytesPerSample = 1;
			break;
		case PcmFormat::S16:
			maFormat = ma_format_s16;
			bytesPerSample = 2;
			break;
		case PcmFormat::F32:
			maFormat = ma_format_f32;
			bytesPerSample = 4;
			break;
		}

		Impl::Entry entry{};
		entry.Pcm.resize(static_cast<size_t>(frameCount) * channels * bytesPerSample);
		std::memcpy(entry.Pcm.data(), frames, entry.Pcm.size());

		entry.Buffer = CreateScope<ma_audio_buffer>();
		ma_audio_buffer_config bufferConfig =
		    ma_audio_buffer_config_init(maFormat, channels, frameCount, entry.Pcm.data(), nullptr);
		bufferConfig.sampleRate = sampleRate; // miniaudio resamples to the device rate on playback
		if (const ma_result result = ma_audio_buffer_init(&bufferConfig, entry.Buffer.get()); result != MA_SUCCESS)
		{
			SS_CORE_ERROR("Audio: cannot wrap PCM ({})", ma_result_description(result));
			return NullInstance;
		}

		entry.Sound = CreateScope<ma_sound>();
		if (const ma_result result = ma_sound_init_from_data_source(&m_Impl->Engine, entry.Buffer.get(),
		                                                            0, nullptr, entry.Sound.get());
		    result != MA_SUCCESS)
		{
			ma_audio_buffer_uninit(entry.Buffer.get());
			SS_CORE_ERROR("Audio: cannot create a sound from PCM ({})", ma_result_description(result));
			return NullInstance;
		}

		const InstanceId id = m_NextInstanceId++;
		m_Impl->Sounds.emplace(id, std::move(entry));
		return id;
	}

	void AudioService::SetInstancePan(const InstanceId id, const float pan)
	{
		if (ma_sound* sound = m_Impl ? m_Impl->Find(id) : nullptr)
		{
			ma_sound_set_pan(sound, pan);
		}
	}

	void AudioService::SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up)
	{
		if (!m_Impl)
		{
			return;
		}
		constexpr ma_uint32 listenerIndex = 0;
		ma_engine_listener_set_position(&m_Impl->Engine, listenerIndex, position.x, position.y, position.z);
		ma_engine_listener_set_direction(&m_Impl->Engine, listenerIndex, forward.x, forward.y, forward.z);
		ma_engine_listener_set_world_up(&m_Impl->Engine, listenerIndex, up.x, up.y, up.z);
	}

	void AudioService::SetInstanceSpatial(const InstanceId id, const bool enabled)
	{
		if (ma_sound* sound = m_Impl ? m_Impl->Find(id) : nullptr)
		{
			ma_sound_set_spatialization_enabled(sound, enabled ? MA_TRUE : MA_FALSE);
		}
	}

	void AudioService::SetInstancePosition(const InstanceId id, const glm::vec3& position)
	{
		if (ma_sound* sound = m_Impl ? m_Impl->Find(id) : nullptr)
		{
			ma_sound_set_position(sound, position.x, position.y, position.z);
		}
	}

	void AudioService::SetInstanceDistances(const InstanceId id, const float minDistance, const float maxDistance)
	{
		if (ma_sound* sound = m_Impl ? m_Impl->Find(id) : nullptr)
		{
			ma_sound_set_min_distance(sound, minDistance);
			ma_sound_set_max_distance(sound, maxDistance);
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
