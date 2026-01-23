#pragma once
#include <cstdint>

namespace Snowstorm
{
	using VisibilityMask = uint32_t;

	namespace Visibility
	{
		static constexpr VisibilityMask None            = 0;
		static constexpr VisibilityMask Scene           = 1u << 0; // editor "Scene"
		static constexpr VisibilityMask Game            = 1u << 1; // runtime "Game"
		static constexpr VisibilityMask MaterialPreview = 1u << 2; // material preview
		static constexpr VisibilityMask All             = 0xFFFF'FFFFu;
	}

	// On renderables (meshes, lights, etc.)
	struct VisibilityComponent
	{
		VisibilityMask Mask = Visibility::Scene; // default: editor scene visible
	};

	// On cameras (what they render)
	struct CameraVisibilityComponent
	{
		VisibilityMask Mask = Visibility::All; // camera sees everything by default
	};

	void RegisterVisibilityComponents();
}
