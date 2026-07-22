#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"
#include "Snowstorm/ECS/Singleton.hpp"
#include "Snowstorm/World/Entity.hpp"

namespace Snowstorm
{
	// Editor-wide "currently selected entity", shared by the hierarchy panel, the inspector, the
	// viewport gizmo, and mouse picking so they all agree on one selection. Lives on the World so
	// any system can read it via GetSingleton; it is editor-only and never serialized.
	class EditorSelectionSingleton final : public Singleton
	{
	public:
		Entity Selected;

		// True while the viewport gizmo is actively being dragged (ImGuizmo::IsUsing()). Set by
		// ViewportDisplaySystem each frame. Systems that also write the selected entity's transform
		// (e.g. RotatorSystem) skip it while this is set, so the animation doesn't fight the manual edit
		// -- the editor-authoring-wins convention (cf. Unreal not ticking an actor you're manipulating).
		bool GizmoActive = false;

		// Editor-wide "currently selected ASSET" (e.g. a .ssmat picked in the Content Browser). Mutually
		// exclusive with the entity selection: the Properties panel shows the entity inspector OR the asset
		// inspector, never both. SelectedAsset == 0 / SelectedAssetType == None means "no asset selected".
		// Use SelectEntity()/SelectAsset() to keep the two in sync (each clears the other).
		AssetHandle SelectedAsset{0};
		AssetType SelectedAssetType = AssetType::None;

		void SelectEntity(const Entity e)
		{
			Selected = e;
			SelectedAsset = AssetHandle{0};
			SelectedAssetType = AssetType::None;
		}

		void SelectAsset(const AssetHandle handle, const AssetType type)
		{
			SelectedAsset = handle;
			SelectedAssetType = type;
			Selected = {};
			GizmoActive = false;
		}
	};
}
