#include <rttr/registration.h>

#include "CameraComponent.hpp"
#include "CameraControllerComponent.hpp"
#include "ComponentRegistry.hpp"
#include "FramebufferComponent.hpp"
#include "MaterialComponent.hpp"
#include "MeshComponent.hpp"
#include "NativeScriptComponent.hpp"
#include "RenderTargetComponent.hpp"
#include "SpriteComponent.hpp"
#include "TagComponent.hpp"
#include "TransformComponent.hpp"
#include "ViewportComponent.hpp"

namespace Snowstorm
{
	using namespace rttr;

	void InitializeRTTR()
	{
		registration::class_<CameraComponent>("Snowstorm::CameraComponent")
			.property("Camera", &CameraComponent::Camera)
			.property("Primary", &CameraComponent::Primary)
			.property("FixedAspectRatio", &CameraComponent::FixedAspectRatio);
		Snowstorm::RegisterComponent<CameraComponent>();

		registration::class_<CameraControllerComponent>("Snowstorm::CameraControllerComponent")
			.property("LookSensitivity", &CameraControllerComponent::LookSensitivity)
			.property("MoveSpeed", &CameraControllerComponent::MoveSpeed)
			.property("RotationEnabled", &CameraControllerComponent::RotationEnabled)
			.property("RotationSpeed", &CameraControllerComponent::RotationSpeed)
			.property("ZoomSpeed", &CameraControllerComponent::ZoomSpeed);
		Snowstorm::RegisterComponent<CameraControllerComponent>();

		registration::class_<MaterialComponent>("Snowstorm::MaterialComponent")
			.constructor()
			.property("MaterialInstance", &MaterialComponent::MaterialInstance);
		Snowstorm::RegisterComponent<MaterialComponent>();

		registration::class_<MeshComponent>("Snowstorm::MeshComponent")
			.constructor()
			.property("MeshInstance", &MeshComponent::MeshInstance);
		Snowstorm::RegisterComponent<MeshComponent>();

		registration::class_<NativeScriptComponent>("Snowstorm::NativeScriptComponent")
			.constructor()
			.property("Instance", &NativeScriptComponent::Instance);
		Snowstorm::RegisterComponent<NativeScriptComponent>();

		registration::class_<RenderTargetComponent>("Snowstorm::RenderTargetComponent")
			.constructor()
			.property("TargetFramebuffer", &RenderTargetComponent::TargetFramebuffer);
		Snowstorm::RegisterComponent<RenderTargetComponent>();

		registration::class_<SpriteComponent>("Snowstorm::SpriteComponent")
			.constructor()
			.property("TextureInstance", &SpriteComponent::TextureInstance)
			.property("TilingFactor", &SpriteComponent::TilingFactor)
			.property("TintColor", &SpriteComponent::TintColor);
		Snowstorm::RegisterComponent<SpriteComponent>();

		registration::class_<TagComponent>("Snowstorm::TagComponent")
			.constructor()
			.property("Tag", &TagComponent::Tag);
		Snowstorm::RegisterComponent<TagComponent>();

		registration::class_<TransformComponent>("Snowstorm::TransformComponent")
			.constructor()
			.property("Position", &TransformComponent::Position)
			.property("Rotation", &TransformComponent::Rotation)
			.property("Scale", &TransformComponent::Scale);
		Snowstorm::RegisterComponent<TransformComponent>();

		registration::class_<ViewportComponent>("Snowstorm::ViewportComponent")
			.constructor()
			.property("Focused", &ViewportComponent::Focused)
			.property("Hovered", &ViewportComponent::Hovered)
			.property("Size", &ViewportComponent::Size);
		Snowstorm::RegisterComponent<ViewportComponent>();
	}
}
