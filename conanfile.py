import os
import subprocess
import sys

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
    project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    vcpkg_dir = os.path.join(project_root, "vcpkg")
    build_dir = os.path.join(project_root, "build")

    # Step 1: Ensure vcpkg is bootstrapped
    if not os.path.exists(os.path.join(vcpkg_dir, "vcpkg.exe")):
        print("Bootstrapping vcpkg...")
        run_command(f"git clone https://github.com/microsoft/vcpkg.git {vcpkg_dir}")
        run_command(f"cd {vcpkg_dir} && .\bootstrap-vcpkg.bat", cwd=project_root)
        run_command(f".\{vcpkg_dir}\vcpkg integrate install", cwd=project_root)

    # Step 2: Install required packages with vcpkg
    print("Installing required packages with vcpkg...")
    packages = "vulkan-headers vulkan-loader vulkan-validationlayers glfw glew spdlog fmt assimp glm stb entt imgui rttr"
    run_command(f"{os.path.join(vcpkg_dir, 'vcpkg')} install {packages}", cwd=project_root)

    # Step 3: Configure CMake with vcpkg toolchain
    print("Configuring CMake with vcpkg toolchain...")
    run_command(f"cmake -S {project_root} -B {build_dir} -DCMAKE_TOOLCHAIN_FILE={os.path.join(vcpkg_dir, 'scripts/buildsystems/vcpkg.cmake')}")

    # Step 4: Build the project
    print("Building the project...")
    run_command(f"cmake --build {build_dir}", cwd=project_root)

    print("\nSetup completed successfully. You can now open the Visual Studio project files.")
    print(f"Path: {os.path.join(build_dir, 'Snowstorm.sln')}")

if __name__ == "__main__":
    main()