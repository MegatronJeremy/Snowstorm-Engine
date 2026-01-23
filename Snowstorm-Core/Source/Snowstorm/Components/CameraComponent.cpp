#include "CameraComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	void RegisterCameraComponent()
	{
		using namespace rttr;

		registration::enumeration<CameraComponent::ProjectionType>("ProjectionType")
		(
			value("Perspective", CameraComponent::ProjectionType::Perspective),
			value("Orthographic", CameraComponent::ProjectionType::Orthographic)
		);

		registration::class_<CameraComponent>("Snowstorm::CameraComponent")
		.property("Projection", &CameraComponent::Projection)
		.property("PerspectiveFOV", &CameraComponent::PerspectiveFOV)
		.property("PerspectiveNear", &CameraComponent::PerspectiveNear)
		.property("PerspectiveFar", &CameraComponent::PerspectiveFar)
		.property("OrthographicSize", &CameraComponent::OrthographicSize)
		.property("OrthographicNear", &CameraComponent::OrthographicNear)
		.property("OrthographicFar", &CameraComponent::OrthographicFar)
		.property("Primary", &CameraComponent::Primary)
		.property("FixedAspectRatio", &CameraComponent::FixedAspectRatio)
		.property("AspectRatio", &CameraComponent::AspectRatio);

		Snowstorm::RegisterComponent<CameraComponent>();
	}
}
