import subprocess
import os
from pathlib import Path

from .utils import write_file_lazy, ensure_dir

def needs_rebuild(depfile, stamp):
    if not depfile.exists() or not stamp.exists():
        return True

    stamp_time = stamp.stat().st_mtime

    for dep in depfile.read_text().split(":", 1)[1].split():
        if Path(dep).stat().st_mtime > stamp_time:
            return True

    return False

def build_shaders(cwd, build_dir, shaders):
    shader_gen_dir         = ensure_dir(build_dir / "shaders")
    shader_gen_spv_dir     = ensure_dir(shader_gen_dir / "spv")
    shader_gen_include_dir = ensure_dir(shader_gen_dir / "include")
    shader_gen_source_dir  = ensure_dir(shader_gen_dir / "src")
    shader_gen_stamp_dir   = ensure_dir(shader_gen_dir / "stamp")
    shader_gen_dep_dir     = ensure_dir(shader_gen_dir / "dep")

    target = "generated-shaders"
    alias = "generated::shaders"

    cmake_path = shader_gen_dir / "CMakeLists.txt"
    cmake_out  = f"add_library({target} OBJECT)\n"
    cmake_out += f"target_include_directories({target} PUBLIC include)\n"
    cmake_out += f"target_compile_options({target} PRIVATE -std=c++26 -Wno-c23-extensions)\n"
    cmake_out += f"target_sources({target} PRIVATE\n"

    for src_file, prefix, stage_flag in shaders:
        cmake_out += f"    src/{prefix}.cpp\n"

        src_path    = cwd / src_file
        spv_path    = shader_gen_spv_dir     / f"{prefix}.spv"
        header_path = shader_gen_include_dir / f"{prefix}.hpp"
        source_path = shader_gen_source_dir  / f"{prefix}.cpp"
        stamp_path  = shader_gen_stamp_dir   / f"{prefix}.stamp"
        dep_path    = shader_gen_dep_dir     / f"{prefix}.depfile"

        if not needs_rebuild(dep_path, stamp_path):
            continue

        print(f"Compiling shader: {src_path} [{stage_flag}] as {prefix}")

        tmp_path = shader_gen_spv_dir / f"{prefix}.spv.tmp"

        cmd  = ["glslang"]
        cmd += ["-V"]
        cmd += ["-S", stage_flag]
        cmd += ["--quiet"]
        cmd += ["-Isrc"]
        cmd += ["--depfile", dep_path]
        cmd += ["--target-env", "vulkan1.4"]
        cmd += ["-o", tmp_path]
        cmd += [str(src_path)]

        res = subprocess.run(cmd)
        if res.returncode != 0 or not tmp_path.exists():
            print("Shader compilation failed")
            os._exit(1)

        # SPIR-V binary

        write_file_lazy(spv_path, tmp_path.read_bytes())
        tmp_path.unlink()
        stamp_path.touch()

        # C++ source

        source_out  = f"#include \"{prefix}.hpp\"\n"
        source_out +=  "\n"
        source_out += f"alignas(uint32_t) static constexpr unsigned char {prefix}_data[] {{\n"
        source_out += f"#embed \"../spv/{prefix}.spv\"\n"
        source_out +=  "};\n"
        source_out += f"extern const std::span<const uint32_t> {prefix}(reinterpret_cast<const uint32_t*>({prefix}_data), sizeof({prefix}_data) / 4);\n"
        write_file_lazy(source_path, source_out)

        # C++ header

        header_out  =  "#include <span>\n"
        header_out +=  "#include <cstdint>\n"
        header_out +=  "\n"
        header_out += f"extern const std::span<const uint32_t> {prefix};\n"
        write_file_lazy(header_path, header_out)

    cmake_out += "    )\n"
    cmake_out += f"add_library({alias} ALIAS {target})\n"
    write_file_lazy(cmake_path, cmake_out)
