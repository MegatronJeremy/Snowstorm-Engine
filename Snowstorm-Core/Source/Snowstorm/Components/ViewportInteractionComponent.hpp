#pragma once

namespace Snowstorm
{
	// TODO Runtime-only editor component - SHOULD ONLY BE SERIALIZED IN DEBUG BUILDS
	struct ViewportInteractionComponent
	{
		bool Focused = false;
		bool Hovered = false;
	};

	inline bool operator==(const ViewportInteractionComponent& lhs, const ViewportInteractionComponent& rhs)
	{
		return lhs.Focused == rhs.Focused && lhs.Hovered == rhs.Hovered;
	}

	void RegisterViewportInteractionComponent();
};