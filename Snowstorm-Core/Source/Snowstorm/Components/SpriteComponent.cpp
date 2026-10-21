#include "SpriteComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::enumeration<SpriteFilter>("SpriteFilter")(
		    value("Nearest", SpriteFilter::Nearest),
		    value("Linear", SpriteFilter::Linear));

		registration::class_<SpriteComponent>("Snowstorm::SpriteComponent")
		    .property("Texture", &SpriteComponent::Texture)(metadata("AssetType", static_cast<int>(AssetType::Texture)))
		    .property("Size", &SpriteComponent::Size)(metadata("Speed", 0.05f))
		    .property("Pivot", &SpriteComponent::Pivot)(metadata("Speed", 0.01f))
		    .property("Tint", &SpriteComponent::Tint)(metadata("Color", true))
		    .property("FlipX", &SpriteComponent::FlipX)
		    .property("FlipY", &SpriteComponent::FlipY)
		    .property("Layer", &SpriteComponent::Layer)
		    .property("Order", &SpriteComponent::Order)
		    .property("Filter", &SpriteComponent::Filter);
	}

	AUTO_REGISTER_COMPONENT(SpriteComponent);
}
