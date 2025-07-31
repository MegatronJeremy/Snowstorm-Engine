import os
import subprocess
import sys
import shutil

def run_command(command, cwd=None):
    """
    Run a shell command in a specified directory and handle errors.
    """
    try:
        subprocess.run(command, cwd=cwd, check=True, shell=True)
    except subprocess.CalledProcessError as e:
        print(f"Error: Command '{command}' failed with exit code {e.returncode}")
        sys.exit(e.returncode)

def main():
    # Paths
    project_root = "."
    vcpkg_dir = os.path.join(project_root, "vcpkg")
    build_dir = os.path.join(project_root, "build")

    # Set VK_ADD_LAYER_PATH for validation layers
    os.environ["VK_ADD_LAYER_PATH"] = os.path.join(vcpkg_dir, "installed", "x64-windows", "bin")

    # Step 1: Ensure vcpkg is bootstrapped
    if not os.path.exists(os.path.join(vcpkg_dir, "vcpkg.exe")):
        print("Bootstrapping vcpkg...")
        run_command(f"git clone https://github.com/microsoft/vcpkg.git {vcpkg_dir}")
        run_command(f"cd {vcpkg_dir} && .\\bootstrap-vcpkg.bat", cwd=project_root)
        run_command(f".\\vcpkg integrate install", cwd=vcpkg_dir)

    # Step 2: Install required packages with vcpkg
    print("Installing required packages with vcpkg...")
    packages = (
        "vulkan vulkan-validationlayers glfw3 glew spdlog fmt "
        "assimp glm stb entt imgui[opengl3-binding,glfw-binding,docking-experimental] rttr "
        "gli volk vulkan-memory-allocator"
    )
    run_command(f"{os.path.join(vcpkg_dir, 'vcpkg')} install {packages} --triplet x64-windows", cwd=project_root)

    # Step 3: Configure CMake with vcpkg toolchain
    print("Configuring CMake with vcpkg toolchain...")
    cmake_command = (
        f"cmake -S {project_root} -B {build_dir} "
        f"-DCMAKE_TOOLCHAIN_FILE={os.path.join(vcpkg_dir, 'scripts/buildsystems/vcpkg.cmake')} "
        f"-T v143"
    )
    run_command(cmake_command)

    print("\nSetup completed successfully. You can now open the Visual Studio project files.")
    print(f"Path: {os.path.join(build_dir, 'Snowstorm.sln')}")

if __name__ == "__main__":
    main()