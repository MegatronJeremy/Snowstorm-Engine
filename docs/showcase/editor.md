# Editor

*Adapted from [Vuk Đorđević's master's thesis (ETF Belgrade, 2026)](https://github.com/MegatronJeremy/RG2/tree/master/master-rad/teza), updated to the editor as it is now.*

`Snowstorm-Editor` is a Dear ImGui dockspace over the same systems the `Snowstorm-Runtime` player runs. Its job in this project is to make the renderer tunable and inspectable while it runs: every render knob is a console variable read per frame, so a configuration found by hand is reproducible from the command line by the benchmark scripts.

![The editor: settings panel, scene viewport, console and performance readout](../images/editor.jpg)

Screenshot of the editor on Sponza, the same image as the top-level README. Its GPU, commit and date were not recorded; the panel layout predates the NERV theme work in the sections below, so treat it as showing the panel set, not the current look.

## Panels

| Panel | Title in the editor | What it does |
| --- | --- | --- |
| Viewport | `// VIEWPORT` | The scene render with ImGuizmo gizmos, click-to-select, and the Play toolbar at top centre |
| Hierarchy | `// SCENE_HIERARCHY` | Entity tree; the amber `+ Create` button and the right-click menu create Empty, Directional Light, Point Light, Spot Light, Cube, Plane and Camera presets |
| Inspector | `// PROPERTIES` | One section per component, drawn from the reflected (RTTR) properties, so a new component appears without editor code; `Add Component` at the bottom |
| Content Browser | `// CONTENT_BROWSER` | Project assets by type; `Rescan` and `New Material` buttons; right-click a scene for `Open Scene` or `Set as Startup Scene` |
| Settings | `// SETTINGS` | Two tabs: render controls (VSync, exposure, technique modes, ray counts, denoiser iterations, upscaler, `render.debugview`) and a read-only Performance tab |
| Console Variables | `// CONSOLE_VARIABLES` | Every CVar with a type-appropriate widget, a name filter and a tooltip carrying the description; startup-only CVars are disabled and say so |
| Console | `// CONSOLE` | Developer console on the backtick key, with Tab autocomplete for commands and CVars and Up/Down history |

Panel titles carry a stable ImGui ID after `###`, so a saved dock layout survives a title change. *View > Reset Window Layout* restores the default dockspace.

## Seeing where the frame goes

The Performance tab shows frame, per-phase and per-system CPU milliseconds and per-pass GPU milliseconds from timestamp scopes, with draw, batch, instance and triangle counts. This is the first place to look for a slow frame; the same GPU scope data feeds `Scripts/perf-bench.py` and, through the Tracy zones, a connected Tracy GUI. *Debug > Capture 1 Frame / Capture 10 Frames* record frames, and *Debug > Select GPU* picks the adapter on a multi-GPU machine.

`render.debugview` selects any intermediate buffer (AO, GI, reflections, normals and the raw and denoised signals) live; the images on the [AO](rt-ambient-occlusion.md), [GI](rt-global-illumination.md) and [denoising](svgf-denoising.md) pages were produced this way.

## Edit, Play and undo

- **Undo and redo** go through `EditorHistorySingleton`: `Ctrl+Z`, `Ctrl+Y` or `Ctrl+Shift+Z`. Menu labels name the command. Entity delete snapshots the entity first, so undo restores the same UUID.
- **Play** (viewport toolbar) runs the scene in the editor (Simulate: the editor camera and selection stay live). In edit mode the simulation systems are skipped so the authored scene stays still; Stop restores the snapshot taken before Play. The chevron beside the button is disabled while playing.
- **Play Standalone** (chevron > *Standalone (Runtime)*) serializes the open scene to `Engine/cache/standalone-play.world` and launches `Snowstorm-Runtime` on it as a detached process, so the scene runs in the editor-free player with its own window. The runtime is looked up beside the editor (a staged package) and then in the sibling build directory with the same configuration. A missing runtime comes back as a toast with the reason.
- **Play indicator.** While playing, the viewport shows a blinking red `PLAY` dot, drawn by `EditorTheme::DrawStatusBlink` (lit for the first half of each second).

## Assets

Files under the project's `assets/` folder are imported when they appear or change (a file watcher; *Rescan* is the manual fallback). Dropping files from the OS onto the Content Browser imports them (models, textures, shaders, audio) through `AssetManagerSingleton::ImportFile`, the single entry point for a file joining the project, so a file lands in `<assets>/<type>/` without overwriting. *File > Import Model...* and *File > Cook Assets* cover the explicit paths; cooking runs the same function as `--cook.assets`.

### Material editor

`New Material` writes a default `.ssmat` (DefaultLit shader, white base colour, no textures) into the asset root and it appears in the Materials tab and in the inspector's picker. Selecting a material opens it in the material inspector:

- texture slots through an asset picker per slot;
- fragment shader from a combo of discovered shaders (with its own *Rescan*) or a typed path;
- **Alpha Mode** `Opaque`, `Mask` or `Blend` (`Blend` draws in the sorted transparent pass behind `render.transparent`; see [`AGENTS.md`](../../AGENTS.md));
- one row per `// @param` declared in a custom shader, with right-click to reset, and the parse error shown in place when a declaration is malformed;
- `Save` writes the `.ssmat`; shader parameters are stored as a sparse map.

## Projects and scenes

`.ssproj` projects open from *File > New Project... / Open Project... / Recent Projects*. The recent list lives in `%APPDATA%/Snowstorm/RecentProjects.json` (newest first, capped at 10). At boot an explicit `startup.project` wins, then `--editor.picker`, then the most recent project that still exists, then the start screen. Scenes are JSON `.world` files: `Ctrl+N`, `Ctrl+O`, `Ctrl+S`, `Ctrl+Shift+S`; an untitled scene falls through Save to Save As. *Set Current Scene as Startup* writes the project's startup scene.

## NERV theme

`EditorTheme::ApplyEvangelion` applies the style: near-black backgrounds, an amber accent, sharp corners, hard borders and dense spacing, with a monospace font loaded from `Engine/Fonts` when one is present (the built-in font otherwise). Panels open with an amber `// LABEL` header; the one prominent action per panel is a full-width amber `PrimaryButton`; alerts and empty states use a red `WarningBanner`. `editor.crt` (CVar) adds scanlines and a corner vignette over every panel, off unless set.

## Navigation

Hold the right mouse button in the viewport to fly: `W A S D` move, `E Q` up and down, `Shift` sprints, `Ctrl` slows, the wheel changes fly speed. Without the button the wheel dollies. `W`, `E`, `R` switch the gizmo between translate, rotate and scale while an entity is selected (and `E`, `Q` fly only while the right button is held); `Escape` clears the selection; `F` frames the selection, `Shift+F` the whole scene, double-click an entity to focus it. The full list is in *Help > Keyboard & Mouse Shortcuts*, which is the authority when this page and the editor differ.

## Not measured

No editor timing, memory or usability number is stated here; none was measured for this page. Panel behaviour above is read from the editor source at the commit below, not from a fresh run on a GPU.

Source read at master `3161042`, 2026-10-17.
