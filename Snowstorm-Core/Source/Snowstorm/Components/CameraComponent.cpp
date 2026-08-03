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
		    // Perspective near/far must be strictly positive (a <=0 near plane makes the projection degenerate).
		    .property("PerspectiveNear", &CameraComponent::PerspectiveNear)(metadata("Min", 0.001f), metadata("Speed", 0.01f))
		    .property("PerspectiveFar", &CameraComponent::PerspectiveFar)(metadata("Min", 0.01f))
		    // Ortho half-height must be > 0. Ortho near/far are intentionally NOT clamped -- an orthographic
		    // range is legitimately negative (the default is -10..10), unlike the perspective near plane.
		    .property("OrthographicSize", &CameraComponent::OrthographicSize)(metadata("Min", 0.001f))
		    .property("OrthographicNear", &CameraComponent::OrthographicNear)
		    .property("OrthographicFar", &CameraComponent::OrthographicFar)
		    .property("Primary", &CameraComponent::Primary)
		    .property("FixedAspectRatio", &CameraComponent::FixedAspectRatio)
		    .property("AspectRatio", &CameraComponent::AspectRatio)(metadata("Min", 0.01f));
	}

	AUTO_REGISTER_COMPONENT(CameraComponent);
}
