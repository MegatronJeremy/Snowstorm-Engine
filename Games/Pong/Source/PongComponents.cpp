#include "PongComponents.hpp"

#include "Snowstorm/Components/ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::class_<PongPaddleComponent>("PongPaddleComponent")
		    .constructor()
		    .property("Side", &PongPaddleComponent::Side)
		    .property("Speed", &PongPaddleComponent::Speed)
		    .property("HalfHeight", &PongPaddleComponent::HalfHeight);

		registration::class_<PongBallComponent>("PongBallComponent")
		    .constructor()
		    .property("Velocity", &PongBallComponent::Velocity)
		    .property("Radius", &PongBallComponent::Radius)
		    .property("ScoreLeft", &PongBallComponent::ScoreLeft)
		    .property("ScoreRight", &PongBallComponent::ScoreRight);
	}

	// The registered name is the serialization key in Pong.world, so it is deliberately the bare name,
	// matching what the scene file already contains.
	AUTO_REGISTER_COMPONENT(PongPaddleComponent);
	AUTO_REGISTER_COMPONENT(PongBallComponent);
}
