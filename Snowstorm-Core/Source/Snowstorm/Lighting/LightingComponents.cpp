#include "LightingComponents.hpp"

#include "Snowstorm/Components/ComponentRegistry.hpp"

#include <rttr/registration.h>

#include <algorithm>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::class_<DirectionalLightComponent>("Snowstorm::DirectionalLightComponent")
		    .property("Direction", &DirectionalLightComponent::Direction)
		    .property("Color", &DirectionalLightComponent::Color)(metadata("Color", true)) // inspector: color picker
		    .property("Intensity", &DirectionalLightComponent::Intensity)(metadata("Min", 0.0f), metadata("Speed", 0.01f))
		    .property("CastShadows", &DirectionalLightComponent::CastShadows); // inspector: checkbox

		registration::class_<PointLightComponent>("Snowstorm::PointLightComponent")
		    .property("Color", &PointLightComponent::Color)(metadata("Color", true))
		    .property("Intensity", &PointLightComponent::Intensity)(metadata("Min", 0.0f), metadata("Speed", 0.01f))
		    .property("Range", &PointLightComponent::Range)(metadata("Min", 0.0f), metadata("Speed", 0.1f));

		registration::class_<SpotLightComponent>("Snowstorm::SpotLightComponent")
		    .property("Color", &SpotLightComponent::Color)(metadata("Color", true))
		    .property("Intensity", &SpotLightComponent::Intensity)(metadata("Min", 0.0f), metadata("Speed", 0.01f))
		    .property("Range", &SpotLightComponent::Range)(metadata("Min", 0.0f), metadata("Speed", 0.1f))
		    .property("InnerAngleDeg", &SpotLightComponent::InnerAngleDeg)(metadata("Min", 0.0f), metadata("Max", 89.0f), metadata("Speed", 0.5f))
		    .property("OuterAngleDeg", &SpotLightComponent::OuterAngleDeg)(metadata("Min", 0.0f), metadata("Max", 89.0f), metadata("Speed", 0.5f))
		    .property("CastShadows", &SpotLightComponent::CastShadows); // inspector: checkbox
	}

	// Spot cone invariant: the outer angle must be >= the inner angle, or the falloff denominator
	// (cos(inner) - cos(outer)) goes non-positive and the cone renders wrong. The gather/shader already
	// clamp defensively, but enforcing it here keeps the inspector value itself consistent (so the field
	// doesn't display an invalid combination). Must be defined before AUTO_REGISTER_COMPONENT below, which
	// instantiates RegisterComponent<SpotLightComponent> and calls this.
	template <>
	void NormalizeComponent<SpotLightComponent>(SpotLightComponent& c)
	{
		c.OuterAngleDeg = std::max(c.OuterAngleDeg, c.InnerAngleDeg);
	}

	AUTO_REGISTER_COMPONENT(DirectionalLightComponent);
	AUTO_REGISTER_COMPONENT(PointLightComponent);
	AUTO_REGISTER_COMPONENT(SpotLightComponent);
}
