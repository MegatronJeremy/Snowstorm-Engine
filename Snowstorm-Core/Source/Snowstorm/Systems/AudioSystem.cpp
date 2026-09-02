#include "AudioSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/AudioSourceComponent.hpp"
#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/World/SimulationStateSingleton.hpp"

namespace Snowstorm
{
	AudioSystem::~AudioSystem()
	{
		// AudioService is application-scoped and outlives this World, so its sounds are not reclaimed just
		// because the World went away. In a packaged runtime this destructor is the whole story; in the
		// editor it almost never runs, because a scene load reuses the same World. That case is handled by
		// the sweep in Execute, not here.
		if (!Application::Exists() || !Application::Get().GetServiceManager().ServiceRegistered<AudioService>())
		{
			return;
		}
		ReleaseAll(Application::Get().GetServiceManager().GetService<AudioService>());
	}

	void AudioSystem::ReleaseAll(AudioService& audio)
	{
		for (auto& [entity, voice] : m_Voices)
		{
			audio.DestroyInstance(voice.Instance);
		}
		m_Voices.clear();
	}

	void AudioSystem::Execute(Timestep)
	{
		auto& audio = ServiceView<AudioService>();
		if (!audio.IsAvailable())
		{
			return;
		}

		auto& reg = m_World->GetRegistry();

		// Authoring a scene should be silent. Like RotatorSystem's animation or the Mandelbrot zoom, the
		// sound is part of the simulation, so it only runs in Play mode; a packaged runtime has no
		// SimulationStateSingleton and always plays. Stopping on the transition (rather than just not
		// starting) is what makes the editor's Stop button actually stop the audio.
		if (m_World->HasSingleton<SimulationStateSingleton>() &&
		    !m_World->GetSingleton<SimulationStateSingleton>().IsPlaying())
		{
			ReleaseAll(audio);
			return;
		}

		auto& assets = SingletonView<AssetManagerSingleton>();

		for (const auto view = View<AudioSourceComponent>(); const entt::entity entity : view)
		{
			const auto& source = reg.Read<AudioSourceComponent>(entity);
			Voice& voice = m_Voices[entity];

			// Rebuild on a clip change, which covers both the first sighting of this entity and an
			// inspector edit swapping the clip out from under a playing sound.
			if (voice.Clip != source.Clip)
			{
				audio.DestroyInstance(voice.Instance);
				voice = Voice{};
				voice.Clip = source.Clip;

				// Zero is "no clip assigned", a normal authoring state. A non-zero handle the registry
				// does not know is a broken reference and worth saying so.
				if (source.Clip != 0)
				{
					if (const std::filesystem::path path = assets.GetAbsolutePath(source.Clip); path.empty())
					{
						SS_CORE_WARN("AudioSource: clip handle {} is not in the asset registry", source.Clip.Value());
					}
					else
					{
						voice.Instance = audio.CreateInstance(path);
					}
				}
			}

			if (voice.Instance == AudioService::NullInstance)
			{
				continue;
			}

			// Pushed every frame rather than on change: these are cheap setters, and it keeps an inspector
			// edit audible immediately without tracking which field moved.
			audio.SetInstanceVolume(voice.Instance, source.Volume);
			audio.SetInstancePitch(voice.Instance, source.Pitch);
			audio.SetInstanceLooping(voice.Instance, source.Loop);

			if (source.PlayOnStart && !voice.Started)
			{
				voice.Started = true;
				audio.Play(voice.Instance);
			}
		}

		// Sweep: destroy voices whose entity no longer exists or no longer emits. Nothing signals component
		// destruction, so without this an entity delete, a scene load or a Play/Stop cycle would strand a
		// playing sound that no id remains to reach, and repeating it would stack copies without bound.
		for (auto it = m_Voices.begin(); it != m_Voices.end();)
		{
			if (reg.valid(it->first) && reg.any_of<AudioSourceComponent>(it->first))
			{
				++it;
				continue;
			}
			audio.DestroyInstance(it->second.Instance);
			it = m_Voices.erase(it);
		}
	}
}
