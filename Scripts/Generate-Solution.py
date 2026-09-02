import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

PACKAGES = [
    "vulkan",
    "vulkan-validationlayers",
    "glfw3",
    "glew",
    "spdlog",
    "fmt",
    "assimp",
    "glm",
    "stb",
    "entt",
    "imgui[vulkan-binding,glfw-binding,docking-experimental,glfw-binding]",
    "imguizmo",
    "rttr",
    "gli",
    "volk",
    "vulkan-memory-allocator",
    "spirv-reflect",
    "nlohmann-json",
    "catch2",
    "tracy",  # real-time frame/sampling profiler (client lib); connect the Tracy GUI to a running build
]

# Extra packages for --with-doom only (SS_ENABLE_DOOM). Deliberately NOT in PACKAGES: the embedded Doom
# is an off-by-default example, so a normal generate should not spend time installing an audio stack it
# will never link. SDL_mixer is what gives doomgeneric sound effects and music (FEATURE_SOUND); the MIDI
# backend for the music comes from the sdl2-mixer overlay in Scripts/vcpkg-overlays/.
DOOM_PACKAGES = [
    "sdl2",
    "sdl2-mixer",
]

# The toolset must resolve to one concrete MSVC version, the same on both sides. vcpkg takes the
# latest installed minor version, a bare "-T v143" takes the VS instance default
# (Microsoft.VCToolsVersion.default.txt), and when those diverge the engine compiles with one
# toolset and links libs built by another: Catch2 then fails with LNK2019 on vectorized-STL symbols
# (__std_search_1, __std_find_last_of_trivial_pos_1) the older toolset's libs do not provide.
# detect_msvc_toolset resolves the version per machine; CMake receives it via "-T", vcpkg via the
# overlay triplet in Scripts/vcpkg-triplets/, which reads SS_MSVC_TOOLSET_VERSION.
#
# A hardcoded version instead fails outright wherever that exact toolset is absent, with CMake
# reporting "does not seem to be installed at .../Microsoft.VCToolsVersion.<ver>.props".
TOOLSET_MAJOR = "v143"

# Set SS_MSVC_TOOLSET to a concrete version (e.g. 14.44.35207) to override detection.
TOOLSET_ENV = "SS_MSVC_TOOLSET"

def run(cmd, cwd=None, env=None):
    print(">", " ".join(cmd))
    try:
        subprocess.run(cmd, cwd=cwd, env=env, check=True)
    except subprocess.CalledProcessError as e:
        sys.exit(e.returncode)

def _version_key(name: str):
    """Numeric sort key for an MSVC version dir. Lexicographic would rank 14.9 above 14.37."""
    return [int(p) if p.isdigit() else -1 for p in name.split(".")]

def find_vs_install() -> Path | None:
    """Newest VS install with the C++ toolset, via vswhere (ships at a fixed path with any VS)."""
    program_files_x86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = Path(program_files_x86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        return None
    try:
        out = subprocess.run(
            [str(vswhere), "-latest", "-products", "*",
             "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
             "-property", "installationPath"],
            capture_output=True, text=True, check=True,
        ).stdout.strip().splitlines()
    except (subprocess.CalledProcessError, OSError):
        return None
    return Path(out[0]) if out else None

def detect_msvc_toolset() -> tuple[str | None, Path | None]:
    """Resolve (toolset_version, vs_install_path).

    Picks the LATEST installed toolset, matching vcpkg's own rule, so the common case is a no-op
    against what vcpkg would have chosen anyway.

    A candidate is accepted when its host-x64 cl.exe is actually present. Note that CMake's failure
    message names VC/Auxiliary/Build/<ver>/Microsoft.VCToolsVersion.<ver>.props, but that file is
    NOT what makes a toolset usable -- on a stock VS 2022 install no versioned props file exists at
    all, and "-T v143,version=<ver>" still configures fine as long as VC/Tools/MSVC/<ver> is there.
    Checking for the props file instead rejects perfectly good toolsets (verified on 14.37.32822).

    Returns (None, ...) when nothing usable is found; the caller then falls back to a bare -T v143.
    """
    override = os.environ.get(TOOLSET_ENV, "").strip()
    vs_install = find_vs_install()

    if override:
        print(f"MSVC toolset: {override} (from {TOOLSET_ENV})")
        return override, vs_install

    if vs_install is None:
        return None, None

    tools_dir = vs_install / "VC" / "Tools" / "MSVC"
    if not tools_dir.is_dir():
        return None, vs_install

    candidates = sorted((d.name for d in tools_dir.iterdir() if d.is_dir()), key=_version_key, reverse=True)
    for version in candidates:
        if (tools_dir / version / "bin" / "Hostx64" / "x64" / "cl.exe").exists():
            print(f"MSVC toolset: {version} (latest installed, at {vs_install})")
            return version, vs_install
        print(f"  skipping MSVC {version}: no host-x64 cl.exe (incomplete install)")

    return None, vs_install

def ensure_vcpkg(vcpkg_dir: Path):
    exe = vcpkg_dir / "vcpkg.exe"
    if exe.exists():
        return exe

    print(f"Bootstrapping vcpkg into: {vcpkg_dir}")
    vcpkg_dir.parent.mkdir(parents=True, exist_ok=True)

    run(["git", "clone", "https://github.com/microsoft/vcpkg.git", str(vcpkg_dir)])
    run([str(vcpkg_dir / "bootstrap-vcpkg.bat")], cwd=vcpkg_dir)

    # Optional but convenient for MSBuild/CMake discovery
    run([str(exe), "integrate", "install"], cwd=vcpkg_dir)

    return exe

def main():
    ap = argparse.ArgumentParser(description="Configure Snowstorm solution (vcpkg + CMake)")
    ap.add_argument("--triplet", default="x64-windows", help="vcpkg triplet (default: x64-windows)")
    ap.add_argument("--build-dir", default="build", help="Build directory (default: build)")
    ap.add_argument("--vcpkg-dir", default="vcpkg", help="vcpkg directory (default: vcpkg under repo)")
    ap.add_argument("--clean", action="store_true", help="Delete build dir before configuring")
    ap.add_argument("--fresh", action="store_true", help="Also delete vcpkg installed/buildtrees (forces full reinstall)")
    ap.add_argument("--generator", default=None, help='Optional CMake generator, e.g. "Visual Studio 17 2022"')
    ap.add_argument("--with-doom", action="store_true",
                    help="Also install the embedded Doom's audio dependencies and configure with "
                         "SS_ENABLE_DOOM=ON. Note doomgeneric is GPL-2.0: such a build cannot be "
                         "redistributed under this project's UNLICENSE.")
    args = ap.parse_args()

    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent  # adjust if your script is elsewhere

    build_dir = (project_root / args.build_dir).resolve()
    vcpkg_dir = (project_root / args.vcpkg_dir).resolve()

    if args.clean and build_dir.exists():
        print(f"Cleaning build dir: {build_dir}")
        shutil.rmtree(build_dir)

    if args.fresh:
        for p in [vcpkg_dir / "installed", vcpkg_dir / "buildtrees", vcpkg_dir / "packages"]:
            if p.exists():
                print(f"Removing: {p}")
                shutil.rmtree(p)

    vcpkg_exe = ensure_vcpkg(vcpkg_dir)

    # Resolve the MSVC toolset ONCE and hand the same version to vcpkg and CMake (see TOOLSET_MAJOR).
    toolset_version, vs_install = detect_msvc_toolset()
    if toolset_version is None:
        print("WARNING: could not detect an installed MSVC toolset; falling back to the VS default "
              f"(-T {TOOLSET_MAJOR}). If dependency links fail with LNK2019 on __std_* symbols, set "
              f"{TOOLSET_ENV} to the version vcpkg built with.", file=sys.stderr)

    # Set VK_ADD_LAYER_PATH so validation layers are discoverable
    env = os.environ.copy()
    env["VK_ADD_LAYER_PATH"] = str(vcpkg_dir / "installed" / args.triplet / "bin")

    # Local overlay triplets: Scripts/vcpkg-triplets/x64-windows.cmake is the stock triplet plus
    # VCPKG_PLATFORM_TOOLSET_VERSION, which it reads from this env var. Overriding the stock triplet
    # NAME keeps "--triplet x64-windows" (and every installed/ path and doc reference) unchanged.
    if toolset_version:
        env["SS_MSVC_TOOLSET_VERSION"] = toolset_version
    overlay_triplets = script_dir / "vcpkg-triplets"
    if overlay_triplets.is_dir():
        env["VCPKG_OVERLAY_TRIPLETS"] = str(overlay_triplets)
        print(f"Using overlay triplets: {overlay_triplets}")

    # Local overlay ports (override upstream vcpkg ports). polyclipping ships here
    # to fetch clipper 6.4.2 from a GitHub mirror over git instead of SourceForge,
    # which a filtering proxy (e.g. Zscaler) blocks. See Scripts/vcpkg-overlays/.
    overlay_ports = script_dir / "vcpkg-overlays"
    if overlay_ports.is_dir():
        env["VCPKG_OVERLAY_PORTS"] = str(overlay_ports)
        print(f"Using overlay ports: {overlay_ports}")

    print("Installing vcpkg packages...")
    packages = PACKAGES + (DOOM_PACKAGES if args.with_doom else [])
    run([str(vcpkg_exe), "install", *packages, "--recurse", "--triplet", args.triplet], env=env, cwd=project_root)

    print("Configuring CMake...")
    build_dir.mkdir(parents=True, exist_ok=True)

    # "v143,version=X" when detected, bare "v143" otherwise (CMake then uses the instance default).
    toolset_arg = f"{TOOLSET_MAJOR},version={toolset_version}" if toolset_version else TOOLSET_MAJOR

    # Pin the VS INSTANCE too. With several VS installs, vswhere -latest and CMake's own instance
    # pick can disagree, and then the version we resolved from one install is looked up in another.
    instance_arg = [f"-DCMAKE_GENERATOR_INSTANCE={vs_install}"] if vs_install else []

    run([
        "cmake",
        "-S", str(project_root),
        "-B", str(build_dir),
        f"-DCMAKE_TOOLCHAIN_FILE={vcpkg_dir / 'scripts' / 'buildsystems' / 'vcpkg.cmake'}",
        "-T", toolset_arg,
        *instance_arg,
        *(["-G", args.generator] if args.generator else []),
        *([f"-DSS_ENABLE_DOOM={'ON' if args.with_doom else 'OFF'}"]),
    ], env=env)

    sln = build_dir / "Snowstorm.sln"
    print("\nDone.")
    print("Build dir :", build_dir)
    print("Solution  :", sln)

if __name__ == "__main__":
    main()
