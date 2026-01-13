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
    "rttr",
    "gli",
    "volk",
    "vulkan-memory-allocator",
    "spirv-reflect",
    "nlohmann-json",
]

def run(cmd, cwd=None, env=None):
    print(">", " ".join(cmd))
    try:
        subprocess.run(cmd, cwd=cwd, env=env, check=True)
    except subprocess.CalledProcessError as e:
        sys.exit(e.returncode)

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

def vcpkg_install(vcpkg_exe: Path, triplet: str, packages: list[str]):
    pkg_str = " ".join(packages)
    run([
        str(vcpkg_exe),
        "install",
        *packages,
        "--recurse",
        "--triplet", triplet
    ])

def configure_cmake(project_root: Path, build_dir: Path, vcpkg_dir: Path, generator: str | None):
    toolchain = vcpkg_dir / "scripts" / "buildsystems" / "vcpkg.cmake"
    args = [
        "cmake",
        "-S", str(project_root),
        "-B", str(build_dir),
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
        "-T", "v143",
    ]

    # If you want to force VS generator explicitly (optional)
    if generator:
        args.extend(["-G", generator])

    run(args)

def main():
    ap = argparse.ArgumentParser(description="Configure Snowstorm solution (vcpkg + CMake)")
    ap.add_argument("--triplet", default="x64-windows", help="vcpkg triplet (default: x64-windows)")
    ap.add_argument("--build-dir", default="build", help="Build directory (default: build)")
    ap.add_argument("--vcpkg-dir", default="vcpkg", help="vcpkg directory (default: vcpkg under repo)")
    ap.add_argument("--clean", action="store_true", help="Delete build dir before configuring")
    ap.add_argument("--fresh", action="store_true", help="Also delete vcpkg installed/buildtrees (forces full reinstall)")
    ap.add_argument("--generator", default=None, help='Optional CMake generator, e.g. "Visual Studio 17 2022"')
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

    # Set VK_ADD_LAYER_PATH so validation layers are discoverable
    env = os.environ.copy()
    env["VK_ADD_LAYER_PATH"] = str(vcpkg_dir / "installed" / args.triplet / "bin")

    print("Installing vcpkg packages...")
    run([str(vcpkg_exe), "install", *PACKAGES, "--recurse", "--triplet", args.triplet], env=env, cwd=project_root)

    print("Configuring CMake...")
    build_dir.mkdir(parents=True, exist_ok=True)
    run([
        "cmake",
        "-S", str(project_root),
        "-B", str(build_dir),
        f"-DCMAKE_TOOLCHAIN_FILE={vcpkg_dir / 'scripts' / 'buildsystems' / 'vcpkg.cmake'}",
        "-T", "v143",
        *(["-G", args.generator] if args.generator else []),
    ], env=env)

    sln = build_dir / "Snowstorm.sln"
    print("\nDone.")
    print("Build dir :", build_dir)
    print("Solution  :", sln)

if __name__ == "__main__":
    main()
