#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"
#include "Snowstorm/Audio/AudioService.hpp"
#include "Snowstorm/ECS/System.hpp"

#include <unordered_map>

namespace Snowstorm
{
	// Turns AudioSourceComponents into playing sounds: loads each entity's clip, applies its authored
	// volume/pitch/loop, and starts it in Play mode. The scene-facing half of the audio system;
	// AudioService owns the device and does the mixing.
	class AudioSystem final : public System
	{
	public:
		explicit AudioSystem(const WorldRef world) : System(world)
		{
		}

		~AudioSystem() override;

		void Execute(Timestep ts) override;

	private:
		// A sound this system created and is responsible for destroying.
		struct Voice
		{
			AudioService::InstanceId Instance = AudioService::NullInstance;
			AssetHandle Clip{0}; // what Instance was created from; a change rebuilds it
			bool Started = false;
		};

		// Deliberately owned by the SYSTEM rather than stored on the entity. A voice has to outlive its
		// component: entity deletion, ClearSceneEntities and the editor's Play/Stop snapshot restore all
		// destroy components without notice, and a per-entity id would be destroyed with them, stranding a
		// still-playing sound that nothing can reach. Holding the map here lets Execute sweep voices whose
		// entity is gone.
		//
		// Keyed on entt::entity, whose value carries a version, so a recycled index is a different key
		// rather than a silent alias onto the previous occupant's voice.
		std::unordered_map<entt::entity, Voice> m_Voices;

		// Stops and forgets every voice. Used on teardown and whenever playback should stop wholesale.
		void ReleaseAll(AudioService& audio);
	};
}
