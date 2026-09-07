#pragma once

#include <glm/glm.hpp>

namespace Snowstorm
{
	// One of the two bats. Side picks which end of the field it defends and which keys drive it.
	struct PongPaddleComponent
	{
		int Side = 0;       // 0 = left (W/S), 1 = right (Up/Down)
		float Speed = 9.0f; // world units per second
		float HalfHeight = 1.0f;
	};

	// The ball, and the match state. The score lives here rather than on a third entity because this is
	// an example: one more component would be one more thing to read before the point lands.
	struct PongBallComponent
	{
		glm::vec3 Velocity{5.0f, 3.0f, 0.0f};
		float Radius = 0.2f;

		int ScoreLeft = 0;
		int ScoreRight = 0;
	};
}
