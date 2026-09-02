#include "AudioSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Audio/AudioService.hpp"
#include "Snowstorm/Components/AudioSourceComponent.hpp"
#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	AudioSystem::~AudioSystem()
	{
		// AudioService outlives this World (it is application-scoped), so its sounds do not go away when
		// the scene does. Without this, every scene load would leak one decoded clip per emitter.
		if (!Application::Exists() || !Application::Get().GetServiceManager().ServiceRegistered<AudioService>())
		{
			return;
		}

		auto& audio = Application::Get().GetServiceManager().GetService<AudioService>();
		auto& reg = m_World->GetRegistry();
		for (const entt::entity entity : reg.view<AudioSourceRuntimeComponent>())
		{
			audio.DestroyInstance(reg.Read<AudioSourceRuntimeComponent>(entity).Instance);
		}
	}

	void AudioSystem::Execute(Timestep)
	{
		auto& audio = ServiceView<AudioService>();
		if (!audio.IsAvailable())
		{
			return;
		}

		auto& reg = m_World->GetRegistry();
		auto& assets = SingletonView<AssetManagerSingleton>();

		for (const auto view = View<AudioSourceComponent>(); const entt::entity entity : view)
		{
			const auto& source = reg.Read<AudioSourceComponent>(entity);
			reg.Ensure<AudioSourceRuntimeComponent>(entity);
			// Untracked get, not Write: nothing observes this component, and marking it changed every
			// frame would grow the change map for every emitter for no reader.
			auto& runtime = reg.get<AudioSourceRuntimeComponent>(entity);

			// (Re)build the instance when the authored clip changes, which covers both the first frame and
			// an inspector edit swapping the clip out from under a playing sound.
			if (runtime.LoadedClip != source.Clip)
			{
				audio.DestroyInstance(runtime.Instance);
				runtime.Instance = AudioService::NullInstance;
				runtime.LoadedClip = source.Clip;
				runtime.StartRequested = false;

				// Zero is "no clip assigned", which is a normal authoring state and not worth a warning.
				// A non-zero handle the registry does not know IS worth one: it means a broken reference.
				if (source.Clip != 0)
				{
					if (const std::filesystem::path path = assets.GetAbsolutePath(source.Clip); path.empty())
					{
						SS_CORE_WARN("AudioSource: clip handle {} is not in the asset registry", source.Clip.Value());
					}
					else
					{
						runtime.Instance = audio.CreateInstance(path);
					}
				}
			}

			if (runtime.Instance == AudioService::NullInstance)
			{
				continue;
			}

			// Pushed every frame rather than on change: these are cheap setters, and it keeps an inspector
			// edit audible immediately without the system having to track which field moved.
			audio.SetInstanceVolume(runtime.Instance, source.Volume);
			audio.SetInstancePitch(runtime.Instance, source.Pitch);
			audio.SetInstanceLooping(runtime.Instance, source.Loop);

			if (source.PlayOnStart && !runtime.StartRequested)
			{
				runtime.StartRequested = true;
				audio.Play(runtime.Instance);
			}
		}
	}
}
