#include "VisibilityComponents.hpp"

#include <rttr/registration.h>

#include "ComponentRegistry.hpp"

namespace Snowstorm
{
	namespace
	{
		// Named bits for the inspector's flag editor. Mirrors the Visibility:: layers; keep in sync.
		FlagBitList VisibilityFlagBits()
		{
			return {
			    {"Scene", Visibility::Scene},
			    {"Game", Visibility::Game},
			    {"MaterialPreview", Visibility::MaterialPreview},
			};
		}
	}

	void RegisterVisibilityComponents()
	{
		using namespace rttr;

		registration::class_<VisibilityComponent>("Snowstorm::VisibilityComponent")
		    .property("Mask", &VisibilityComponent::Mask)(
		        metadata("Flags", VisibilityFlagBits()) // inspector: checkbox dropdown over named layers
		    );

		registration::class_<CameraVisibilityComponent>("Snowstorm::CameraVisibilityComponent")
		    .property("Mask", &CameraVisibilityComponent::Mask)(
		        metadata("Flags", VisibilityFlagBits()));

		Snowstorm::RegisterComponent<VisibilityComponent>();
		Snowstorm::RegisterComponent<CameraVisibilityComponent>();
	}
}
