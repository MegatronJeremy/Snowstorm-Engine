#include "AudioSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/AudioListenerComponent.hpp"
#include "Snowstorm/Components/AudioSourceComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
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

	void AudioSystem::UpdateListener(AudioService& audio)
	{
		auto& reg = m_World->GetRegistry();

		entt::entity listener = entt::null;
		uint32_t count = 0;
		for (const entt::entity entity : reg.view<AudioListenerComponent, TransformComponent>())
		{
			if (!reg.Read<AudioListenerComponent>(entity).Enabled)
			{
				continue;
			}
			if (listener == entt::null)
			{
				listener = entity;
			}
			++count;
		}

		if (listener == entt::null)
		{
			// Spatial sounds still play, panned against a listener sitting at the origin facing -Z. Said
			// once rather than per frame, and only when something actually depends on it.
			if (!m_WarnedNoListener)
			{
				m_WarnedNoListener = true;
				SS_CORE_WARN("Audio: no entity has an AudioListenerComponent; spatial sounds are panned "
				             "against the world origin. Add one to the camera.");
			}
			return;
		}

		if (count > 1 && !m_WarnedManyListeners)
		{
			m_WarnedManyListeners = true;
			SS_CORE_WARN("Audio: {} enabled AudioListenerComponents; using the first. There is one ear.", count);
		}

		// Orientation comes from the transform MATRIX, not from re-deriving pitch/yaw: the matrix applies
		// the component's own Y->X->Z rotation order and so accounts for roll, which a pitch/yaw pair
		// silently drops. Column 2 is the local +Z axis and the engine looks down -Z; column 1 is up.
		// Normalised because a scaled transform would otherwise hand miniaudio a non-unit direction.
		const auto& transform = reg.Read<TransformComponent>(listener);
		const glm::mat4 m = transform.GetTransformMatrix();
		const glm::vec3 forward = glm::normalize(-glm::vec3(m[2]));
		const glm::vec3 up = glm::normalize(glm::vec3(m[1]));

		audio.SetListener(transform.Position, forward, up);
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

		UpdateListener(audio);

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

			audio.SetInstanceSpatial(voice.Instance, source.Spatial);
			if (source.Spatial)
			{
				audio.SetInstanceDistances(voice.Instance, source.MinDistance, source.MaxDistance);
				// An emitter without a transform has no position to be at, so it stays wherever it was
				// last put rather than snapping to the origin.
				if (const auto* transform = reg.try_get<TransformComponent>(entity))
				{
					audio.SetInstancePosition(voice.Instance, transform->Position);
				}
			}

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
