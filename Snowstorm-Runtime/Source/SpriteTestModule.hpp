#pragma once

#include "Snowstorm/Game/GameModule.hpp"

#include <string>

namespace Snowstorm
{
	// The player's debug.sprite_test hook as a game module: spawns two overlapping sprites from an image so
	// the sprite pass has something to draw in a headless run. Opaque copy at the canvas top-left, then a
	// tinted half-transparent copy offset over it on a higher layer, so one quality.capture image shows
	// orientation, blending and layer order at once. Also the smallest example of an IGameModule.
	class SpriteTestModule final : public IGameModule
	{
	public:
		explicit SpriteTestModule(std::string imagePath);

		[[nodiscard]] const char* Name() const override { return "SpriteTest"; }

		void OnAttach(World& world) override;

	private:
		std::string m_ImagePath;
	};
}
