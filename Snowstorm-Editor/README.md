# Snowstorm-Editor: play modes and project selection

## Play modes

`SimulationStateSingleton::Mode` is `Edit`, `Play` or `PlayGame`. `Play` is the in-editor run where the editor camera and selection stay live (Unreal's Simulate; the toolbar entry is "In Editor (Simulate)"); Standalone (#67) launches `Snowstorm-Runtime` and leaves the editor world untouched. `PlayGame` is reserved for possessed play-in-editor (#68: the game's camera and input replace the editor's) and nothing selects it yet. `IsPlaying()` is true for both play modes, so the snapshot/restore and the `RunsInEditMode()` gate cover them alike; `IsPossessing()` is true only in `PlayGame`. The toolbar's *In Editor (Possess)* starts `PlayGame` with `InputStateSingleton::GameCaptured` set: editor-only bindings gate on `EditorKeysBlocked()`, the editor fly camera stands down and the cursor locks. `StepPlayInput` (`Input/PlayInputRouting.hpp`, pure, `PlayInputRoutingTests`) decides capture: Escape or Shift+F1 releases, Escape again stops, a viewport click recaptures, and an active text field keeps its own keys. Not yet in place: the game view target (the editor camera still renders) and a PlayerStart spawn, so the viewport shows the editor camera. Cursor lock and key routing are unverified on a GPU. The remaining steps are planned in the shared workspace's `design/68-play-in-editor.md`.

## Editor project selection

The editor keeps a per-user recent-projects list in `%APPDATA%/Snowstorm/RecentProjects.json`
(`RecentProjects.hpp`: newest first, capped at 10, written whenever a project opens). Boot order,
decided by the pure `RecentProjects::DecideBoot`: an explicit `startup.project` (CLI/env/cfg, anything
but the default) wins; else `--editor.picker` shows the start screen; else the most recent project that
still exists opens directly; else the start screen shows. The start screen is a modal over the
default project (recent list, New, Open, Continue); *File > Recent Projects* offers the same list.
The Runtime keeps its implicit CWD project, and the Editor still falls back to one when the
.ssproj is unreadable, so `Project::GetActive()` is non-null before the picker is dismissed.

Opening a scene places the Scene-view camera from the first of: `camera.override`, the
`<scene>.world.editor` sidecar (rewritten on scene close), the scene's Primary camera pose
(`FindScenePrimaryCamera`), a fit to the renderable bounds. The bounds fit stays last because in a scene whose floor spans the bounds it can land
at floor height, leaving the floor edge-on and invisible.
