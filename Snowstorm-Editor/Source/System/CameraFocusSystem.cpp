#include "CameraFocusSystem.hpp"

#include "Snowstorm/Core/KeyCodes.hpp"
#include "Snowstorm/Input/InputStateSingleton.hpp"
#include "Snowstorm/Math/Bounds.hpp"
#include "Snowstorm/Render/SceneBounds.hpp"
#include "Snowstorm/World/EditorSelectionSingleton.hpp"

namespace Snowstorm
{
	void CameraFocusSystem::Execute(Timestep)
	{
		const auto& input = SingletonView<InputStateSingleton>();

		// Edge-triggered: act only on the frame F goes down, and never while a text field has the keyboard.
		if (input.WantCaptureKeyboard || !input.PressedThisFrame.test(static_cast<size_t>(Key::F)))
		{
			return;
		}

		const bool shift = input.Down.test(static_cast<size_t>(Key::LeftShift)) ||
		                   input.Down.test(static_cast<size_t>(Key::RightShift));

		// Shift+F frames the whole scene; F frames the current selection (falling back to the whole
		// scene if nothing is selected, which is the least surprising behaviour).
		AABB target;
		bool haveTarget = false;
		if (!shift)
		{
			const Entity selected = SingletonView<EditorSelectionSingleton>().Selected;
			if (selected.IsValid())
			{
				haveTarget = ComputeEntityRenderableAABB(*m_World, selected.Handle(), target);
			}
		}
		if (!haveTarget)
		{
			haveTarget = ComputeWorldRenderableAABB(*m_World, target);
		}
		if (!haveTarget)
		{
			return; // nothing renderable to frame
		}

		FramePrimaryCameraOnAABB(*m_World, target);
	}
}
