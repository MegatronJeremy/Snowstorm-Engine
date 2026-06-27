#include "CameraComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::enumeration<CameraComponent::ProjectionType>("ProjectionType")(
		    value("Perspective", CameraComponent::ProjectionType::Perspective),
		    value("Orthographic", CameraComponent::ProjectionType::Orthographic));

		registration::class_<CameraComponent>("Snowstorm::CameraComponent")
		    .property("Projection", &CameraComponent::Projection)
		    // FOV is stored in radians; clamp the slider to ~5deg..~175deg so it can't go degenerate.
		    .property("PerspectiveFOV", &CameraComponent::PerspectiveFOV)(metadata("Min", 0.0873f), metadata("Max", 3.054f))
		    .property("PerspectiveNear", &CameraComponent::PerspectiveNear)
		    .property("PerspectiveFar", &CameraComponent::PerspectiveFar)
		    .property("OrthographicSize", &CameraComponent::OrthographicSize)
		    .property("OrthographicNear", &CameraComponent::OrthographicNear)
		    .property("OrthographicFar", &CameraComponent::OrthographicFar)
		    .property("Primary", &CameraComponent::Primary)
		    .property("FixedAspectRatio", &CameraComponent::FixedAspectRatio)
		    .property("AspectRatio", &CameraComponent::AspectRatio);
	}

	AUTO_REGISTER_COMPONENT(CameraComponent);
}
