#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	// Turns AudioSourceComponents into playing sounds: loads each entity's clip once, applies its authored
	// volume/pitch/loop, and honours PlayOnStart. The scene-facing half of the audio system; AudioService
	// owns the device and does the mixing.
	class AudioSystem final : public System
	{
	public:
		explicit AudioSystem(const WorldRef world) : System(world)
		{
		}

		~AudioSystem() override;

		void Execute(Timestep ts) override;
	};
}
