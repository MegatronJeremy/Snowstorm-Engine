#pragma once

namespace Snowstorm
{
	// Marker tag for entities that must NOT be written to a scene file and must SURVIVE a scene load
	// (i.e. they belong to the editor/runtime, not the scene). The serializer skips any entity carrying
	// this tag, and World::ClearSceneEntities keeps them while wiping scene content. This is the seam for
	// engine-owned entities that live above the scene — chiefly the editor's persistent Scene-view camera
	// and viewport (cf. Unity's HideFlags.DontSave / editor-owned Scene camera, Unreal's editor viewport
	// client camera). Intentionally NOT RTTR-registered: it is an internal marker, not authored data, so
	// it never appears in the inspector or the serialized component list.
	struct DoNotSerializeComponent
	{
		// A single byte so the type is non-empty: the engine's TrackedRegistry::emplace returns T&, but
		// entt's empty-type storage optimization makes emplace<T>() return void for a zero-field struct,
		// which won't bind to that reference. The value itself is unused — presence of the tag is the data.
		bool Tag = true;
	};
}
