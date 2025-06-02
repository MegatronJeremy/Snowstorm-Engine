#include "CameraComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	void RegisterCameraComponent()
	{
		using namespace rttr;

		registration::class_<CameraComponent>("Snowstorm::CameraComponent")
			.property("Camera", &CameraComponent::Camera)
			.property("Primary", &CameraComponent::Primary)
			.property("FixedAspectRatio", &CameraComponent::FixedAspectRatio);

		registration::enumeration<SceneCamera::ProjectionType>("Snowstorm::ProjectionType")
		(
			value("Perspective", SceneCamera::ProjectionType::Perspective),
			value("Orthographic", SceneCamera::ProjectionType::Orthographic)
		);

		registration::class_<SceneCamera>("Snowstorm::SceneCamera")
			.property("PerspectiveFOV", &SceneCamera::GetPerspectiveVerticalFOV, &SceneCamera::SetPerspectiveVerticalFOV)
			.property("PerspectiveNear", &SceneCamera::GetPerspectiveNearClip, &SceneCamera::SetPerspectiveNearClip)
			.property("PerspectiveFar", &SceneCamera::GetPerspectiveFarClip, &SceneCamera::SetPerspectiveFarClip)
			.property("OrthographicSize", &SceneCamera::GetOrthographicSize, &SceneCamera::SetOrthographicSize)
			.property("OrthographicNear", &SceneCamera::GetOrthographicNearClip, &SceneCamera::SetOrthographicNearClip)
			.property("OrthographicFar", &SceneCamera::GetOrthographicFarClip, &SceneCamera::SetOrthographicFarClip)
			.property("ProjectionType", &SceneCamera::GetProjectionType, &SceneCamera::SetProjectionType);

		Snowstorm::RegisterComponent<CameraComponent>();
	}
}
