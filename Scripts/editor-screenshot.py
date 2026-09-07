#!/usr/bin/env python3
"""Capture a screenshot of the running editor, including the ImGui dockspace.

The engine's own quality capture cannot do this. It is an IViewportEffect operating on the viewport's
present target, so it fires before ImGui composites the panels around it, and what lands on disk is
the scene alone. Everything outside the 3D viewport (hierarchy, inspector, performance panel, console)
exists only in the swapchain image, so the only way to get it is from outside the process.

This launches the editor, waits for the window to appear and the scene to load, grabs the window
rectangle off the desktop, and kills the process. It needs a real desktop session: there is no
headless path to a screenshot of a GUI.

    py Scripts/editor-screenshot.py [--out docs/images/editor.png] [--wait 45]

The window takes focus while this runs. Do not type into other applications during the wait.
"""
import argparse
import ctypes
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "Snowstorm-Editor" / "Debug" / "Snowstorm-Editor.exe"

user32 = ctypes.windll.user32
user32.SetProcessDPIAware()


def find_window_for_pid(pid: int):
    """Top-level visible window owned by pid, with a non-empty title."""
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value != pid or not user32.IsWindowVisible(hwnd):
            return True
        n = user32.GetWindowTextLengthW(hwnd)
        if n:
            buf = ctypes.create_unicode_buffer(n + 1)
            user32.GetWindowTextW(hwnd, buf, n + 1)
            found.append((hwnd, buf.value))
        return True

    user32.EnumWindows(cb, 0)
    return found


def window_rect(hwnd):
    r = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="docs/images/editor.png")
    ap.add_argument("--wait", type=int, default=45,
                    help="seconds to let the editor load the scene and compile shaders before grabbing")
    ap.add_argument("--keep-open", action="store_true", help="do not kill the editor afterwards")
    args = ap.parse_args()

    if not EXE.exists():
        print(f"FAIL: {EXE} not found (build the editor first)")
        return 1

    from PIL import ImageGrab

    env_note = "launching editor; it will take focus for about %d s" % args.wait
    print(env_note)
    proc = subprocess.Popen([str(EXE)], cwd=str(ROOT))

    hwnd = None
    deadline = time.time() + args.wait
    while time.time() < deadline:
        time.sleep(1.0)
        if proc.poll() is not None:
            print(f"FAIL: editor exited early with code {proc.returncode}")
            return 1
        wins = find_window_for_pid(proc.pid)
        if wins:
            hwnd = wins[0][0]
            title = wins[0][1]
    if hwnd is None:
        print("FAIL: no visible window appeared for the editor process")
        proc.kill()
        return 1

    # Bring it forward so nothing overlaps it, then let the compositor settle before grabbing.
    user32.ShowWindow(hwnd, 3)  # SW_MAXIMIZE
    user32.SetForegroundWindow(hwnd)
    time.sleep(3.0)

    left, top, right, bottom = window_rect(hwnd)
    img = ImageGrab.grab(bbox=(left, top, right, bottom), all_screens=True)

    if not args.keep_open:
        proc.kill()
        proc.wait(timeout=10)

    out = ROOT / args.out
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out)
    print(f"wrote {out}  ({img.width}x{img.height}, {out.stat().st_size / 1024:.0f} KB)  title={title!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
