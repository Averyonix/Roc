import subprocess
from pathlib import Path

from .utils import write_file_lazy, ensure_dir

def generate_wayland_protocols(wayland_dir, protocols):

    wayland_scanner = "wayland-scanner" # Wayland scanner executable
    wayland_src     = ensure_dir(wayland_dir / "src")
    wayland_include = ensure_dir(wayland_dir / "include/wayland")
    wayland_client_include = ensure_dir(wayland_include / "client")
    wayland_server_include = ensure_dir(wayland_include / "server")

    alias = "generated::wayland"
    target = "generated-wayland"

    cmake_file = wayland_dir / "CMakeLists.txt"

    cmake = f"add_library({target} OBJECT\n"

    for xml_path in protocols:

        name = xml_path.stem
        header_name = f"{name}.h"

        # Generate client header
        header_path = wayland_client_include / header_name
        if not header_path.exists():
            cmd = [wayland_scanner, "client-header", xml_path, header_name]
            print(f"Generating wayland client header: {header_name}")
            subprocess.run(cmd, cwd = header_path.parent)

        # Generate server header
        header_path = wayland_server_include / header_name
        if not header_path.exists():
            cmd = [wayland_scanner, "server-header", xml_path, header_name]
            print(f"Generating wayland server header: {header_name}")
            subprocess.run(cmd, cwd = header_path.parent)

        # Generate source
        source_name = f"{name}-protocol.c"
        source_path = wayland_src / source_name
        if not source_path.exists():
            cmd = [wayland_scanner, "private-code", xml_path, source_name]
            print(f"Generating wayland source: {source_name}")
            subprocess.run(cmd, cwd = wayland_src)

        # Add source to CMakeLists
        cmake += f"    \"src/{source_name}\"\n"

    cmake += "    )\n"
    cmake += f"target_include_directories({target} PUBLIC include)\n"
    cmake += f"add_library({alias} ALIAS {target})\n"

    write_file_lazy(cmake_file, cmake)
