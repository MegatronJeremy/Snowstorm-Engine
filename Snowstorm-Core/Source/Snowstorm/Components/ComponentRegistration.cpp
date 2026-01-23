#include <rttr/registration.h>

#include "CameraComponent.hpp"
#include "CameraControllerComponent.hpp"
#include "CameraTargetComponent.hpp"
#include "ComponentRegistry.hpp"
#include "IDComponent.hpp"
#include "MaterialComponent.hpp"
#include "MaterialOverridesComponent.hpp"
#include "MeshComponent.hpp"
#include "NativeScriptComponent.hpp"
#include "RenderTargetComponent.hpp"
#include "SpriteComponent.hpp"
#include "TagComponent.hpp"
#include "TransformComponent.hpp"
#include "ViewportComponent.hpp"
#include "ViewportInteractionComponent.hpp"
#include "VisibilityComponents.hpp"

#include "Snowstorm/Lighting/LightingComponents.hpp"

namespace Snowstorm
{
	using namespace rttr;

	void InitializeRTTR()
	{
		// TODO move all of these to functions (and automate it somehow)
		RegisterCameraComponent();
		RegisterCameraControllerComponent();
		RegisterMeshComponent();
		RegisterMaterialComponent();
		RegisterMaterialOverridesComponent();
		RegisterLightingComponents();
		RegisterIDComponent();
		RegisterViewportComponent();
		RegisterViewportInteractionComponent();
		RegisterCameraTargetComponent();
		RegisterVisibilityComponents();

		registration::class_<NativeScriptComponent>("Snowstorm::NativeScriptComponent")
			.constructor()
			.property("Instance", &NativeScriptComponent::Instance);
		Snowstorm::RegisterComponent<NativeScriptComponent>();

		registration::class_<SpriteComponent>("Snowstorm::SpriteComponent")
			.constructor()
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
	}
}
