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
args = parser.parse_args()

# -----------------------------------------------------------------------------

program_name = "fen"

# -----------------------------------------------------------------------------

def ensure_dir(path):
    os.makedirs(path, exist_ok=True)
    return Path(path)

build_dir  = ensure_dir(".build")
vendor_dir = ensure_dir(build_dir / "3rdparty")

# -----------------------------------------------------------------------------

def git_fetch(dir, repo, branch, dumb=False):
    if not dir.exists():
        cmd = ["git", "clone", repo, "--branch", branch, dir]
        print(cmd)
        subprocess.run(cmd)
    elif args.update:
        cmd = ["git", "pull"]
        print(f"{cmd} @ {dir}")
        subprocess.run(cmd, cwd = dir)
    return dir

# -----------------------------------------------------------------------------

git_fetch(vendor_dir / "backward-cpp", "https://github.com/bombela/backward-cpp.git", "master")

# -----------------------------------------------------------------------------

git_fetch(vendor_dir / "magic-enum", "https://github.com/Neargye/magic_enum.git", "master")

# -----------------------------------------------------------------------------

git_fetch(vendor_dir / "stb", "https://github.com/nothings/stb.git", "master")

# -----------------------------------------------------------------------------

git_fetch(vendor_dir / "glm", "https://github.com/g-truc/glm.git", "master")

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
    for category in os.listdir(system_protocol_dir):
        category_path = system_protocol_dir / category
        if category_path.is_dir():
            for subfolder in os.listdir(category_path):
                subfolder_path = category_path / subfolder
                if subfolder_path.is_dir():
                    for name in os.listdir(subfolder_path):
                        if Path(name).suffix == ".xml":
                            wayland_protocols += [(subfolder_path / name, Path(name).stem)]

    wlroots_protocol_dir = wlroots_src_dir / "protocol"
    for name in os.listdir(wlroots_protocol_dir):
        if Path(name).suffix == ".xml":
            wayland_protocols += [(wlroots_protocol_dir.absolute() / name, Path(name).stem)]

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
