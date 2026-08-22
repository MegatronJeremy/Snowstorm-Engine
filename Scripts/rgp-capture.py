#!/usr/bin/env python3
"""Bootstrap the Radeon Developer Tool Suite (RGP/RDP/RDS) and drive a headless RGP capture.

The RGA occupancy gate (rga-occupancy.py) is STATIC: it models VGPR-limited occupancy from register
pressure. This script is the RUNTIME counterpart: it captures an actual RGP profile (achieved
wavefront occupancy, cache/bandwidth, per-instruction stall reasons) headlessly, which is the only
way to confirm whether a shader (e.g. GIDenoise.comp, the one shader the static gate flags at 8/16
waves) is truly occupancy/latency-bound rather than ALU-bound before optimizing it.

Headless capture pipeline (no GUI, no human clicks):
  1. RadeonDeveloperServiceCLI  -- enables dev-mode driver.
  2. Launch the editor with SS_SMOKE_FRAMES + the GI env so it renders GIDenoise for N frames and
     exits on its own (same headless-run hook smoke-test.py uses).
  3. RadeonDeveloperPanelCLI    -- triggers a capture mid-run, writes a .rgp.

Capture flags are wired from the tool's real --help (verified, RGP 2.7): `-p <process>`,
`--rgp-auto-capture=frame:N`, `--rgp-instruction-tracing`, `--rgp-counter-collection`, `-o`.
Resolved unknown: RadeonDeveloperPanelCLI outputs ONLY a `.rgp` -- there is no CSV/text metric
export, so the runtime numbers still have to be read in the RGP GUI (or by parsing the undocumented
.rgp). The capture is headless; the analysis is not.

Unlike RGA, the RDTS zip has no published SHA-256, so integrity can't be pinned; the URL is pinned
by dated build.

Usage:
    py Scripts/rgp-capture.py bootstrap   # download + extract RDTS into Tools/rdts/, list tools
    py Scripts/rgp-capture.py discover    # run --help/--version on the located CLIs (resolve flags)
    py Scripts/rgp-capture.py editor-cmd  # print the exact headless editor launch for GIDenoise
    py Scripts/rgp-capture.py capture     # (stub) drive the capture -- completed after `discover`
"""
import argparse
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
import zipfile
from pathlib import Path

CAPTURE_PROCESS = "Snowstorm-Editor"  # RadeonDeveloperPanelCLI -p filter
CAPTURE_FRAME = 120                   # auto-capture at this app frame (past the 15-frame warmup)

# Pinned RDTS build (dated, from gpuopen.com release notes). No SHA-256 is published for this zip.
RDTS_VERSION = "2026-05-28-1806"
RDTS_URL = "https://gpuopen.com/download/RadeonDeveloperToolSuite-{v}.zip"

# Tool executables we expect inside the extracted suite (located by rglob, not a fixed layout).
TOOLS = ["RadeonDeveloperPanelCLI", "RadeonDeveloperServiceCLI", "RadeonDeveloperPanel",
         "RadeonDeveloperService", "RadeonGPUProfiler"]

# The engine config that puts GIDenoise.comp on the hot path (from EngineCVars + perf-bench.py).
EDITOR_ENV = {
    "SS_RENDER_GI_MODE": "2",       # RT global illumination -> runs the GI denoiser
    "SS_RENDER_SHADOWS_MODE": "2",
    "SS_RENDER_AA": "2",          # TAA (RT effects assume it)
    "SS_STARTUP_SCENE": "Projects/Sandbox/assets/scenes/Sponza.world",
}


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def rdts_dir() -> Path:
    return repo_root() / "Tools" / "rdts" / RDTS_VERSION


def locate_tools(root: Path) -> dict[str, Path]:
    """Map tool base name -> first matching .exe found under the extraction dir."""
    found = {}
    if root.is_dir():
        for name in TOOLS:
            for p in root.rglob(f"{name}.exe"):
                found[name] = p
                break
    return found


def bootstrap_rdts() -> dict[str, Path]:
    """Download + extract RDTS into Tools/rdts/<version>/; idempotent. Returns located tools."""
    dest = rdts_dir()
    tools = locate_tools(dest)
    if tools:
        return tools

    dest.mkdir(parents=True, exist_ok=True)
    url = RDTS_URL.format(v=RDTS_VERSION)
    zip_path = dest / f"RadeonDeveloperToolSuite-{RDTS_VERSION}.zip"
    print(f"  bootstrapping RDTS {RDTS_VERSION} (one-time, large; no published SHA-256 to verify)")
    print(f"    from {url}")
    try:
        with urllib.request.urlopen(url, timeout=1800) as resp, open(zip_path, "wb") as f:
            shutil.copyfileobj(resp, f)
    except (urllib.error.URLError, OSError) as e:
        print(f"    download failed: {e}")
        return {}
    try:
        with zipfile.ZipFile(zip_path) as z:
            z.extractall(dest)
    except zipfile.BadZipFile as e:
        print(f"    extract failed: {e}")
        return {}
    finally:
        zip_path.unlink(missing_ok=True)

    tools = locate_tools(dest)
    if not tools:
        print(f"    extracted, but no known tool .exe found under {dest}")
    return tools


def ensure_rdts() -> dict[str, Path]:
    """SS_RDTS env (a folder) or the cached bootstrap, else download."""
    env = os.environ.get("SS_RDTS")
    if env and Path(env).is_dir():
        tools = locate_tools(Path(env))
        if tools:
            return tools
    tools = locate_tools(rdts_dir())
    return tools if tools else bootstrap_rdts()


def print_tools(tools: dict[str, Path]) -> None:
    if not tools:
        print("  no RDTS tools located.")
        return
    for name in TOOLS:
        print(f"  {name:<28} {tools.get(name, '(not found)')}")


def editor_command() -> tuple[list[str], dict[str, str]]:
    """The exact headless editor launch that renders GIDenoise for N frames then exits."""
    exe = repo_root() / "build" / "Snowstorm-Editor" / "Debug" / "Snowstorm-Editor.exe"
    env = dict(EDITOR_ENV)
    env.setdefault("SS_SMOKE_FRAMES", "600")  # run ~600 frames unattended, then exit cleanly
    return [str(exe)], env


def build_capture_cmd(tools: dict[str, Path], out_rgp: Path,
                      process: str = CAPTURE_PROCESS, frame: int = CAPTURE_FRAME) -> list[str] | None:
    """Assemble the RadeonDeveloperPanelCLI capture invocation (flags verified from its --help).

    -m profiling            RGP profile (vs raytracing/memory/crash/clocks)
    -p <process>            connect only to the editor process
    --rgp-auto-capture=frame:N   capture at the app's Nth frame, unattended
    --rgp-instruction-tracing    per-instruction timing -> stall reasons (what we need for GIDenoise)
    --rgp-counter-collection     hardware counters -> achieved occupancy / cache / bandwidth
    -o <out.rgp>            output profile. NOTE: only a .rgp is produced; there is no CSV/text
                            metric export, so reading the numbers still needs the RGP GUI.
    """
    panel = tools.get("RadeonDeveloperPanelCLI")
    if not panel:
        return None
    # --remote-host 127.0.0.1 is required: without it the panel doesn't default to the local RDS and
    # fails with "Failed to initialize capture API" (verified).
    return [str(panel), "-m", "profiling", "-p", process,
            "--remote-host", "127.0.0.1",
            f"--rgp-auto-capture=frame:{frame}",
            "--rgp-instruction-tracing", "--rgp-counter-collection",
            "-o", str(out_rgp)]


def cmd_bootstrap() -> int:
    tools = ensure_rdts()
    print(f"RDTS dir : {rdts_dir()}")
    print_tools(tools)
    return 0 if tools else 1


def cmd_discover() -> int:
    tools = ensure_rdts()
    if not tools:
        print("RDTS not available; run bootstrap first (or set SS_RDTS).")
        return 1
    for name in ("RadeonDeveloperPanelCLI", "RadeonDeveloperServiceCLI"):
        exe = tools.get(name)
        if not exe:
            print(f"\n=== {name}: not found in suite ===")
            continue
        print(f"\n=== {name} --help ===")
        for flag in ("--help", "-h", "--version"):
            try:
                r = subprocess.run([str(exe), flag], capture_output=True, text=True, timeout=30)
                out = (r.stdout + r.stderr).strip()
                if out:
                    print(out[:4000])
                    break
            except (subprocess.TimeoutExpired, OSError) as e:
                print(f"  ({flag} failed: {e})")
    return 0


def cmd_editor_cmd() -> int:
    argv, env = editor_command()
    print("Editor launch (headless GIDenoise render):")
    print("  env: " + " ".join(f"{k}={v}" for k, v in env.items()))
    print("  cmd: " + " ".join(argv))
    exe = Path(argv[0])
    if not exe.exists():
        print(f"  NOTE: {exe} not built yet.")
    return 0


def cmd_capture() -> int:
    """Headless RGP capture: start the dev service, start the panel (waiting for the editor),
    launch the editor to render GIDenoise for N frames and exit, collect the .rgp."""
    tools = ensure_rdts()
    if not tools:
        print("RDTS not available; run bootstrap first.")
        return 1
    rds = tools.get("RadeonDeveloperServiceCLI")
    if not rds or "RadeonDeveloperPanelCLI" not in tools:
        print("Missing RadeonDeveloperServiceCLI / RadeonDeveloperPanelCLI in the suite.")
        return 1
    argv, env = editor_command()
    if not Path(argv[0]).exists():
        print(f"Editor not built: {argv[0]}")
        return 1

    out_dir = repo_root() / "Tools" / "rdts" / "captures"
    out_dir.mkdir(parents=True, exist_ok=True)
    out_rgp = out_dir / "GIDenoise.rgp"
    out_rgp.unlink(missing_ok=True)
    cap = build_capture_cmd(tools, out_rgp)

    def _log(name):
        return open(out_dir / f"{name}.log", "w")

    print(f"Capture   : {CAPTURE_PROCESS} @ frame {CAPTURE_FRAME} -> {out_rgp.name}")
    rds_p = subprocess.Popen([str(rds)], stdout=_log("rds"), stderr=subprocess.STDOUT)
    time.sleep(4)  # let RDS fully come up before the panel connects
    panel_p = subprocess.Popen(cap, stdout=_log("panel"), stderr=subprocess.STDOUT)
    time.sleep(3)  # let the panel init the capture API + start waiting for the process
    full_env = os.environ.copy()
    full_env.update(env)
    ed_p = subprocess.Popen(argv, cwd=str(repo_root()), env=full_env,
                            stdout=_log("editor"), stderr=subprocess.STDOUT)
    print(f"  editor pid {ed_p.pid}, rendering {env['SS_SMOKE_FRAMES']} frames...")
    try:
        ed_p.wait(timeout=300)
    except subprocess.TimeoutExpired:
        ed_p.kill()
        print("  editor timed out (killed)")
    time.sleep(10)  # let the panel finalize the .rgp after the capture frame
    for p in (panel_p, rds_p):
        p.terminate()
        try:
            p.wait(timeout=10)
        except subprocess.TimeoutExpired:
            p.kill()

    if out_rgp.exists() and out_rgp.stat().st_size > 0:
        print(f"  OK: {out_rgp}  ({out_rgp.stat().st_size // 1024} KB)")
        print(f"  Open in RGP: \"{tools.get('RadeonGPUProfiler', 'RadeonGPUProfiler.exe')}\" \"{out_rgp}\"")
        return 0
    print(f"  FAIL: no .rgp produced. Check {out_dir}/panel.log and editor.log.")
    return 1


def main() -> int:
    ap = argparse.ArgumentParser(description="Bootstrap RDTS + drive a headless RGP capture.")
    ap.add_argument("action", nargs="?", default="bootstrap",
                    choices=["bootstrap", "discover", "editor-cmd", "capture"])
    args = ap.parse_args()
    return {"bootstrap": cmd_bootstrap, "discover": cmd_discover,
            "editor-cmd": cmd_editor_cmd, "capture": cmd_capture}[args.action]()


if __name__ == "__main__":
    sys.exit(main())
