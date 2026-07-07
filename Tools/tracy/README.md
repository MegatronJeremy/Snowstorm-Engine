# Tracy profiler GUI

`tracy-profiler.exe` is the Tracy **viewer** — the desktop app that connects to a running Snowstorm
build and shows the live, cross-thread, per-frame timeline. It is committed here (like `Tools/dxc/`)
so the GUI version stays matched to the Tracy **client** the engine links.

- **Version: 0.13.1** — must match the `tracy` vcpkg client version (see `Scripts/Generate-Solution.py`
  and `vcpkg/ports/tracy/vcpkg.json`). If you bump the vcpkg client, replace this exe with the matching
  GUI from https://github.com/wolfpld/tracy/releases so the wire protocol versions agree.

## Use

1. Build and run **Snowstorm-Editor in Debug** (Tracy is compiled in only when `TRACY_ENABLE` is set,
   which the CMake enables for the Debug config; Release has zero profiler overhead).
2. Run `Tools/tracy/tracy-profiler.exe` and click **Connect** (defaults to `127.0.0.1`). It latches onto
   the running editor and streams live.
3. The frame-time graph is driven by `SS_PROFILE_FRAME_MARK` (end of the run loop). Zones come from
   `SS_PROFILE_SCOPE` / `SS_PROFILE_FUNCTION`: frame-loop phases, every ECS system, and JobSystem worker
   tasks (load a scene / drag Sponza to see workers light up on their own thread rows).

## Headless alternative

No GUI needed for automated/offline analysis: the same macros also feed a chrome://tracing JSON writer.
Set `profile.capture_frames=N` (env `SS_PROFILE_CAPTURE_FRAMES`) and `profile.capture_path` to dump a
trace, then open it in https://ui.perfetto.dev or parse it directly.
