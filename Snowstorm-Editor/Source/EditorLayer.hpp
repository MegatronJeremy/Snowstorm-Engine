#pragma once

#include <Snowstorm.h>

namespace Snowstorm
{
	class EditorLayer final : public Layer
	{
	public:
		EditorLayer();
		~EditorLayer() override = default;

		void OnAttach() override;
		void OnDetach() override;

		void OnUpdate(Timestep ts) override;

	private:
		bool TryLoadWorldFromFile(const std::string& scenePath);
		void LoadOrCreateStartupWorld();

		// Queue a scene to load at the next frame boundary (see OnUpdate's pending-scene handling). Loading
		// is deferred so the editor keeps presenting frames — clear color + loading overlay — instead of
		// blocking in-place; also the safe point to free the old scene's GPU resources. Caller ensures the
		// path exists.
		void RequestSceneLoad(const std::string& scenePath);

		// Handle the scene.bake CVar: if set, populate a fresh scene ("stress" recipe or a model path),
		// save it to Assets/Scenes/<name>.world, close the app, and return true. Returns false (no-op)
		// when no bake was requested, so the normal startup-load path proceeds.
		bool BakeRequestedScene();

		// Force-load every material-override texture in the active world so they register in the
		// bindless set NOW (at load), not lazily during command recording — which would invalidate the
		// bound descriptor set. Call after building/loading a scene, before the first render.
		void PrewarmSceneTextures() const;

		bool SaveWorldToFile(const std::string& scenePath) const;
		bool SaveActiveScene() const;

		void RegisterEditorSystems() const;
		void CreateMainViewportEntity();

		void CreateDemoEntities() const;
		void CreateCameraEntities() const;

		// Per-scene editor camera viewpoint, stored as editor-only metadata in a "<scene>.editor" sidecar
		// (kept out of the serialized scene, cf. Unreal per-level editor camera / Godot editor state). The
		// editor Scene-view camera is host-owned and persists across loads, so without this every scene
		// would open at the same position; the sidecar restores each scene's last-saved viewpoint. Save
		// writes the primary editor camera's transform; Load applies it (returns false if no sidecar).
		void SaveEditorCameraSidecar(const std::string& scenePath) const;
		bool LoadEditorCameraSidecar(const std::string& scenePath) const;

		// The primary editor Scene-view camera entity (Primary CameraComponent + DoNotSerialize). Invalid
		// if it doesn't exist yet. Single lookup point for the per-scene camera save/restore.
		[[nodiscard]] Entity FindEditorCamera() const;

		// Position the primary camera so the whole scene's renderable bounds are in view and fit the
		// near/far planes to its size. Pure camera move (no lights, no save) — shares the framing math
		// with the editor's Frame command so an unknown-scale import (e.g. Sponza) is never off-screen.
		void FrameImportedSceneCamera() const;

		// Add a default key + fill directional light rig as ordinary scene entities. Imported models
		// carry no lights (lighting is a scene-authoring decision), so a fresh showcase scene needs this
		// to be lit. Separate from import + framing so each concern stays independent.
		void AddDefaultLightRig() const;

	private:
		Ref<World> m_ActiveWorld;

		Entity m_RenderTargetEntity;

		std::string m_ActiveScenePath;

		// Scene open requested from a UI system (e.g. Content Browser). Executed at the next frame
		// boundary in OnUpdate, NOT inline: tearing the old scene down mid-frame destroys GPU
		// resources (descriptor sets, meshes) that the in-progress frame's render pass still binds.
		std::string m_PendingScenePath;
		bool m_HasPendingScene = false;

		// Frames that have gone through OnUpdate (≈ presented). Used to hold the startup scene load until at
		// least one empty frame has presented, so the window shows editor chrome + a loading overlay instead
		// of a white/frozen framebuffer while the first (heavy) scene loads.
		uint64_t m_FramesPresented = 0;

		// Play/Edit transition tracking. On Edit->Play we snapshot the world to m_PlaySnapshot (scene JSON);
		// on Play->Stop we restore it, so edits made while playing are discarded (UE model). m_WasPlaying is
		// last frame's mode so OnUpdate can detect the edge.
		bool m_WasPlaying = false;
		std::string m_PlaySnapshot;
	};
}
