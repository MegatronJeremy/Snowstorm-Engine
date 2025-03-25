from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout
from conan.tools.files import copy
import os

class SnowstormConan(ConanFile):
    name = "snowstorm"
    version = "0.1"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain", "VirtualRunEnv"
    
    default_options = {
        "*:shared": True,
        "spirv-tools/*:shared": False
    }

    def requirements(self):
        self.requires("vulkan-headers/1.3.239.0")
        self.requires("vulkan-loader/1.3.239.0")
        self.requires("vulkan-validationlayers/1.3.239.0")
        self.requires("glfw/3.4")
        self.requires("glew/2.2.0")
        self.requires("spdlog/1.15.0")
        self.requires("fmt/11.0.2")
        self.requires("assimp/5.4.3")
        self.requires("glm/cci.20230113")
        self.requires("stb/cci.20230920")
        self.requires("entt/3.14.0")
        self.requires("imgui/cci.20230105+1.89.2.docking")
        self.requires("rttr/0.9.6")

    def generate(self):
        # Define the output directory for DLLs based on build type
        build_type = str(self.settings.build_type)
        output_dir = os.path.join(self.build_folder, "Snowstorm-Editor", build_type)
        os.makedirs(output_dir, exist_ok=True)

        # Copy ImGui bindings
        copy(self, "*glfw*", 
             os.path.join(self.dependencies["imgui"].package_folder, "res", "bindings"), 
             os.path.join(self.source_folder, "Snowstorm-Editor", "Source", "ImGui"))
        copy(self, "*opengl3*", 
             os.path.join(self.dependencies["imgui"].package_folder, "res", "bindings"), 
             os.path.join(self.source_folder, "Snowstorm-Editor", "Source", "ImGui"))

        # Copy DLLs from shared library dependencies (Windows only)
        if self.settings.os == "Windows":
            for dep in self.dependencies.values():
                if dep.options.get_safe("shared", False):  # Check if the dependency is shared
                    # Copy all .dll files from the dependency's bin folder
                    copy(self, "*.dll", 
                         src=dep.cpp_info.bindirs[0], 
                         dst=output_dir, 
                         keep_path=False)
                    # Debug output to verify copied files
                    self.output.info(f"Copied DLLs for {dep.ref.name} to {output_dir}")

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()