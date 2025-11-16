#!/bin/python

import os
import shutil
from pathlib import Path
import subprocess
import argparse
import filecmp

parser = argparse.ArgumentParser()
parser.add_argument("-U", "--update", action="store_true", help="Update")
parser.add_argument("-C", "--configure", action="store_true", help="Force configure")
parser.add_argument("-B", "--build", action="store_true", help="Build")
parser.add_argument("-R", "--release", action="store_true", help="Release")
parser.add_argument("-I", "--install", action="store_true", help="Install")
parser.add_argument("--asan", action="store_true", help="Enable Address Sanitizer")
parser.add_argument("--git-slangc", action="store_true", help="Use git shader-slang")
args = parser.parse_args()

# -----------------------------------------------------------------------------

program_name = "wroc"

# -----------------------------------------------------------------------------

def ensure_dir(path: Path) -> Path:
    os.makedirs(path, exist_ok=True)
    return Path(path)

build_dir  = ensure_dir(".build")
vendor_dir = ensure_dir(build_dir / "3rdparty")

# -----------------------------------------------------------------------------

def write_file_lazy(path: Path, data: str | bytes):
    try:
        if type(data) is str:
            existing = path.read_text()
        else:
            existing = path.read_bytes()
        if existing == data:
            return
    except FileNotFoundError:
        pass

    if type(data) is str:
        path.write_text(data)
    else:
        path.write_bytes(data)

# -----------------------------------------------------------------------------

def git_fetch(dir: Path, repo: str, branch: str, dumb: bool = False) -> Path:
    if not dir.exists():
        cmd = ["git", "clone", repo, "--branch", branch]
        if not dumb:
            cmd += ["--depth", "1", "--recursive"]
        cmd += [dir]
        print(cmd)
        subprocess.run(cmd)
    elif args.update:
        cmd = ["git", "pull"]
        print(f"{cmd} @ {dir}")
        subprocess.run(cmd, cwd = dir)

        cmd = ["git", "submodule", "update", "--init", "--recursive"]
        print(f"{cmd} @ {dir}")
        subprocess.run(cmd, cwd = dir)

    return dir

# -----------------------------------------------------------------------------

def cmake_build(src_dir: Path, build_dir: Path, install_dir, opts: list[str]):
    if not build_dir.exists() or args.update:
        cmd  = ["cmake", "-B", build_dir.absolute(), "-G", "Ninja"]
        cmd += [f"-DCMAKE_INSTALL_PREFIX={install_dir.absolute()}"]
        cmd += ["-DCMAKE_INSTALL_MESSAGE=LAZY"]
        cmd += opts
        print(cmd)
        subprocess.run(cmd, cwd=src_dir)

    if not install_dir.exists() or args.update:
        cmd = ["cmake", "--build", build_dir.absolute(), "--target", "install"]
        print(cmd)
        subprocess.run(cmd, cwd=src_dir)

# -----------------------------------------------------------------------------

git_fetch(vendor_dir / "backward-cpp", "https://github.com/bombela/backward-cpp.git", "master")

# -----------------------------------------------------------------------------

git_fetch(vendor_dir / "magic-enum", "https://github.com/Neargye/magic_enum.git", "master")

# -----------------------------------------------------------------------------

git_fetch(vendor_dir / "stb", "https://github.com/nothings/stb.git", "master")

# -----------------------------------------------------------------------------

git_fetch(vendor_dir / "glm", "https://github.com/g-truc/glm.git", "master")

# -----------------------------------------------------------------------------

git_fetch(vendor_dir / "unordered-dense", "https://github.com/martinus/unordered_dense.git", "main")

# -----------------------------------------------------------------------------

git_fetch(vendor_dir / "vulkan-headers",           "https://github.com/KhronosGroup/Vulkan-Headers.git",                    "main")
git_fetch(vendor_dir / "vulkan-utility-libraries", "https://github.com/KhronosGroup/Vulkan-Utility-Libraries.git",          "main")
git_fetch(vendor_dir / "vulkan-memory-allocator",  "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git", "master")

git_fetch(vendor_dir/ "vkwsi", "https://github.com/Darianopolis/vk-wsi.git", "main")

# -----------------------------------------------------------------------------

git_fetch(vendor_dir / "sol2", "https://github.com/ThePhD/sol2.git", "develop")

def build_luajit():
    source_dir = vendor_dir / "luajit"

    git_fetch(source_dir, "https://luajit.org/git/luajit.git", "v2.1", dumb=True)

    if not (source_dir / "src/libluajit.a").exists() or args.update:
        subprocess.run(["make", "-j"], cwd = source_dir)

build_luajit()

# -----------------------------------------------------------------------------

git_fetch(vendor_dir / "wayland-protocol", "https://gitlab.freedesktop.org/wayland/wayland.git", "main")

# -----------------------------------------------------------------------------

def build_slang() -> Path:
    source_dir  = git_fetch(vendor_dir / "slang", "https://github.com/shader-slang/slang.git", "master")
    build_dir   =           vendor_dir / "slang-build"
    install_dir =           vendor_dir / "slang-install"

    cmake_build(source_dir, build_dir, install_dir, [])
    return install_dir / "bin/slangc"

if args.git_slangc:
    slangc = build_slang()
else:
    slangc = None

# -----------------------------------------------------------------------------

wlroots_src_dir = vendor_dir / "wlroots"

def build_wlroots():
    version = "0.20"
    git_ref = "master"

    git_fetch(wlroots_src_dir, "https://gitlab.freedesktop.org/wlroots/wlroots.git", git_ref)

build_wlroots()

# -----------------------------------------------------------------------------

def list_wayland_protocols():
    wayland_protocols = []
    system_protocol_dir = Path("/usr/share/wayland-protocols")

    wayland_protocols.append((system_protocol_dir / "stable/xdg-shell/xdg-shell.xml", "xdg-shell"))
    wayland_protocols.append((system_protocol_dir / "unstable/xdg-decoration/xdg-decoration-unstable-v1.xml", "xdg-decoration-unstable-v1"))
    wayland_protocols.append((system_protocol_dir / "stable/linux-dmabuf/linux-dmabuf-v1.xml", "linux-dmabuf-v1"))

    return wayland_protocols

def generate_wayland_protocols():

    wayland_scanner = "wayland-scanner"                   # Wayland scanner executable
    wayland_dir = vendor_dir / "wayland"                  #
    wayland_src = ensure_dir(wayland_dir / "src")         # Directory for generate sources
    wayland_include = ensure_dir(wayland_dir / "include") # Directory for generate headers

    cmake_target_name = "wayland-header"
    cmake_file = wayland_dir / "CMakeLists.txt"

    if cmake_file.exists() and not args.update:
        return

    with open(cmake_file, "w") as cmakelists:
        cmakelists.write(f"add_library({cmake_target_name}\n")

        for xml_path, name in list_wayland_protocols():

            # Generate client header
            header_name = f"{name}-client-protocol.h"
            header_path = wayland_include / header_name
            if not header_path.exists():
                cmd = [wayland_scanner, "client-header", xml_path, header_name]
                print(f"Generating wayland client header: {header_name}")
                subprocess.run(cmd, cwd = wayland_include)

            # Generate server header
            header_name = f"{name}-protocol.h"
            header_path = wayland_include / header_name
            if not header_path.exists():
                cmd = [wayland_scanner, "server-header", xml_path, header_name]
                print(f"Generating wayland server header: {header_name}")
                subprocess.run(cmd, cwd = wayland_include)

            # Generate source
            source_name = f"{name}-protocol.c"
            source_path = wayland_src / source_name
            if not source_path.exists():
                cmd = [wayland_scanner, "private-code", xml_path, source_name]
                print(f"Generating wayland source: {source_name}")
                subprocess.run(cmd, cwd = wayland_src)

            # Add source to CMakeLists
            cmakelists.write(f"    \"src/{source_name}\"\n")

        cmakelists.write("    )\n")
        cmakelists.write(f"target_include_directories({cmake_target_name} PUBLIC include)\n")

generate_wayland_protocols()

# -----------------------------------------------------------------------------

def build_shaders():
    shaders = [
        ("src/wroc/shaders/blit.slang",  "wroc_shader_blit")
    ]

    shader_include_dirs = [
        "src"
    ]

    shader_gen_dir = ensure_dir(build_dir / "shaders")
    shader_gen_spv_dir     = ensure_dir(shader_gen_dir / "spv")
    shader_gen_include_dir = ensure_dir(shader_gen_dir / "include")
    shader_gen_source_dir  = ensure_dir(shader_gen_dir / "src")

    header_path = shader_gen_include_dir / "wroc_shaders.hpp"
    source_path = shader_gen_source_dir  / "wroc_shaders.cpp"

    header_out = ""
    source_out = ""

    header_out += "#include <span>\n"
    header_out += "#include <cstdint>\n"

    source_out += f"#include \"{header_path.name}\"\n"

    for shader_src, prefix in shaders:
        tmp_path    = shader_gen_spv_dir / f"{prefix}.spv.tmp"
        spv_path    = shader_gen_spv_dir / f"{prefix}.spv"

        cmd  = ["slangc"]
        cmd += ["-o", tmp_path]
        cmd += ["-target", "spirv"]
        cmd += ["-fvk-use-entrypoint-name"]
        cmd += ["-emit-spirv-directly"]
        cmd += ["-matrix-layout-column-major"]
        cmd += ["-force-glsl-scalar-layout"]
        for i in shader_include_dirs:
            cmd += [f"-I{i}"]

        cmd += [shader_src]

        subprocess.run(cmd, executable=slangc)

        write_file_lazy(spv_path, tmp_path.read_bytes())
        tmp_path.unlink()

        source_out += "\n"
        source_out += f"alignas(uint32_t) static constexpr char {prefix}_data[] {{\n"
        source_out += f"#embed \"../spv/{prefix}.spv\"\n"
        source_out +=  "};\n"
        source_out += f"const std::span<const uint32_t> {prefix}(reinterpret_cast<const uint32_t*>({prefix}_data), sizeof({prefix}_data) / 4);\n"

        header_out += "\n"
        header_out += f"extern const std::span<const uint32_t> {prefix};\n"

    write_file_lazy(header_path, header_out)
    write_file_lazy(source_path, source_out)

build_shaders()

# -----------------------------------------------------------------------------

build_type   = "Debug" if not args.release else "Release"
c_compiler   = "clang"
cxx_compiler = "clang++"
linker_type  = "MOLD"

cmake_dir = build_dir / build_type.lower()

configure_ok = True

if ((args.build or args.install) and not cmake_dir.exists()) or args.configure:
    cmd  = ["cmake", "--fresh", "-B", cmake_dir, "-G", "Ninja", f"-DVENDOR_DIR={vendor_dir}", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    cmd += [f"-DCMAKE_C_COMPILER={c_compiler}", f"-DCMAKE_CXX_COMPILER={cxx_compiler}", f"-DCMAKE_LINKER_TYPE={linker_type}"]
    cmd += [f"-DCMAKE_BUILD_TYPE={build_type}"]
    cmd += [f"-DPROJECT_NAME={program_name}"]
    if args.asan:
        cmd += ["-DUSE_ASAN=1"]

    print(cmd)
    configure_ok = 0 == subprocess.run(cmd).returncode

if configure_ok and (args.build or args.install):
    subprocess.run(["cmake", "--build", cmake_dir])

# -----------------------------------------------------------------------------

def install_file(file: Path, target: Path):
    if target.exists():
        if filecmp.cmp(file, target):
            return
        os.remove(target)
    print(f"Installing [{file}] to [{target}]")
    shutil.copy2(file, target)

if args.install:
    local_bin_dir  = ensure_dir(os.path.expanduser("~/.local/bin"))
    xdg_portal_dir = ensure_dir(os.path.expanduser("~/.config/xdg-desktop-portal"))

    install_file(cmake_dir / program_name, local_bin_dir / program_name)
    install_file("resources/portals.conf", xdg_portal_dir / f"{program_name}-portals.conf")
