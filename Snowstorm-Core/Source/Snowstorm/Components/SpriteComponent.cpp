#include "SpriteComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::class_<SpriteComponent>("Snowstorm::SpriteComponent")
		    .constructor()
		    .property("Position", &SpriteComponent::Position)
		    .property("Size", &SpriteComponent::Size)(metadata("Min", 0.0f))
		    .property("UVRect", &SpriteComponent::UVRect)
		    .property("TintColor", &SpriteComponent::TintColor)(metadata("Color", true)) // inspector: color picker
		    .property("RotationDeg", &SpriteComponent::RotationDeg)(metadata("Speed", 1.0f))
		    .property("Layer", &SpriteComponent::Layer)
		    .property("Visible", &SpriteComponent::Visible);
	}

	AUTO_REGISTER_COMPONENT(SpriteComponent);
}
