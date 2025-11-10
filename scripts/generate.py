#!/usr/bin/env python3

import xml.etree.ElementTree as ET
from pathlib import Path
from typing import List, Dict, Any
import os

# ------------------------------------------------------------------------------

next_interface_id: int = 0

class Interface:
    def __init__(self, name: str, version: int):
        global next_interface_id

        self.name = name
        self.version = version
        self.enums:    List[Dict[str, Any]] = []
        self.requests: List[Dict[str, Any]] = []
        self.events:   List[Dict[str, Any]] = []

        self.interface_id = next_interface_id
        next_interface_id += 1

# ------------------------------------------------------------------------------

def parse_protocol_file(xml_path: Path) -> List[Interface]:
    tree = ET.parse(xml_path)
    root = tree.getroot()
    if root.tag != "protocol":
        raise ValueError(f"{xml_path}: invalid root <{root.tag}>")

    interfaces: List[Interface] = []

    for iface_el in root.findall("interface"):
        iface = Interface(
            name=iface_el.attrib["name"],
            version=int(iface_el.attrib.get("version", 1))
        )

        for req in iface_el.findall("request"):
            iface.requests.append(parse_message(req))

        for evt in iface_el.findall("event"):
            iface.events.append(parse_message(evt))

        for enum_el in iface_el.findall("enum"):
            iface.enums.append(parse_enum(enum_el))

        interfaces.append(iface)

    return interfaces


def parse_message(el: ET.Element) -> Dict[str, Any]:
    msg = {"name": el.attrib["name"], "args": []}
    for arg in el.findall("arg"):
        msg["args"].append({
            "name": arg.attrib["name"],
            "type": arg.attrib["type"],
            "interface": arg.attrib.get("interface"),
            "enum": arg.attrib.get("enum"),
        })
    return msg


def parse_enum(el: ET.Element) -> Dict[str, Any]:
    enum = {
        "name": el.attrib["name"],
        "entries": [],
        "bitfield": el.attrib.get("bitfield") == "true"
    }
    for entry in el.findall("entry"):
        enum["entries"].append({
            "name": entry.attrib["name"],
            "value": entry.attrib["value"]
        })
    return enum

# ---- Code generation ---------------------------------------------------------

CPP_HEADER = """#pragma once
#include "compositor/protocol/wayland_core.hpp"

namespace wayland::server
{

"""

CPP_FOOTER = "} // namespace wayland::server\n"

def cpp_sanitize_identifier(name: str) -> str:
    if name[0].isdigit():
        return "_" + name

    if name in ("default", "export", "auto"):
        return name + "_"

    return name.replace("-", "_")

def get_enum_name(iface: Interface, enum: str):
    if str(enum).find(".") != -1:
        return f"{enum}".replace(".", "_")
    return f"{iface.name}_{enum}"

def cpp_type(iface: Interface, arg: Dict[str, str]) -> str:
    enum = arg.get("enum")
    if enum:
        return get_enum_name(iface, enum)

    t = arg["type"]
    if t == "int":
        return "i32"
    if t == "fd":
        return "int"
    if t == "uint":
        return "u32"
    if t == "fixed":
        return "f64"
    if t == "string":
        return "std::string_view"
    if t == "object":
        iface = arg.get("interface")
        return f"{iface}*" if iface else "Object*"
    if t == "new_id":
        iface = arg.get("interface")
        return f"{iface}*" if iface else "NewId"
    if t == "array":
        return "std::span<const u8>"

    return "int"

def emit_enum(enum: Dict[str, Any], iface_name: str) -> str:
    name = f"{iface_name}_{enum['name']}"
    lines = [f"enum class {name} : u32\n{{"]
    for entry in enum["entries"]:
        enum_name = cpp_sanitize_identifier(entry["name"])
        lines.append(f"    {enum_name} = {entry['value']},")
    lines.append("};\n")
    if enum["bitfield"]:
        lines.append(f"DECORATE_FLAG_ENUM({name})\n")
    return "\n".join(lines)

def arg_parameter_name(iface: Interface, arg: Dict[str, str], decl: bool, stub: bool = False):
    name = arg['name']
    if decl:
        if name not in ('x', 'y', 'id'):
            type = cpp_type(iface, arg)
            if len(name) > 1 and type.find(name) != -1:
                return ""
    arg_str = f"/* {name} */" if stub else name
    return f" {arg_str}"

def emit_message_parameters(iface: Interface, msg: Dict[str, Any], kind: str, decl: bool, stub: bool) -> str:
    args = []
    args.append("Client*" if decl else "Client* client")
    for arg in msg["args"]:
        args.append(f"{cpp_type(iface, arg)}{arg_parameter_name(iface, arg, decl)}")
    return ", ".join(args)


def emit_message_declaration(iface: Interface, msg: Dict[str, Any], kind: str, stub: bool) -> str:
    arglist = emit_message_parameters(iface, msg, kind, True, stub)
    line_end = " {}" if stub else ";"

    return f"    void {cpp_sanitize_identifier(msg['name'])}({arglist}){line_end}"

def emit_interface_forward_enums(f, iface: Interface) -> str:
    for enum in iface.enums:
        f.write( f"enum class {iface.name}_{enum['name']} : u32;\n")

def emit_interface(iface: Interface, stub: bool) -> str:
    lines = []

    # Enums first
    for enum in iface.enums:
        lines.append(emit_enum(enum, iface.name))

    # Struct with requests/events
    lines.append(f"struct {iface.name} : Object\n{{")

    lines.append(f"    {iface.name}(): Object({iface.interface_id}) {{}}")
    lines.append(f"    {iface.name}(Display* display): Object({iface.interface_id}, display_allocate_id(display)) {{}}")
    lines.append("")

    lines.append(f"    static constexpr std::string_view InterfaceName = \"{iface.name}\";")
    lines.append(f"    static constexpr u32 Version = {iface.version};")
    lines.append("")

    if iface.requests:
        lines.append("    /* requests */")
        for req in iface.requests:
            lines.append(emit_message_declaration(iface, req, "request", stub))
        if iface.events:
            lines.append("")
    if iface.events:
        lines.append("    /* events */")
        for evt in iface.events:
            lines.append(emit_message_declaration(iface, evt, "event", False))
    lines.append("};\n")

    return "\n".join(lines)

# ---- Request dispatch --------------------------------------------------------

def emit_parameter_parse(iface: Interface, arg: dict) -> str:
    enum = arg.get("enum")
    if enum:
        return f"message.read_enum<{get_enum_name(iface, enum)}>()"

    t = arg["type"]

    if t in ("int", "fd"):
        return "message.read_int()"
    if t == "uint":
        return "message.read_uint()"
    if t == "fixed":
        return "message.read_fixed()"
    if t == "string":
        return "message.read_string()"
    if t == "object":
        iface_name = arg.get("interface")
        if iface_name:
            return f"message.read_object<{iface_name}>(client, {iface.interface_id})"
        raise RuntimeError("Expected object type")
    if t == "new_id":
        iface = arg.get("interface")
        if iface:
            if iface == "Object":
                print("ERROR")
            return f"message.read_new_id<{iface}>(client)"
        return "message.read_untyped_new_id(client)"
    if t == "array":
        return "message.read_array()"
    if t == "enum":
        enum = arg.get("enum")
        if enum:
            return f"message.read_enum<{enum}>()"
        return "message.read_int()"

    raise RuntimeError("Unknown type")

def emit_request_dispatch(interfaces: list[object]) -> str:
    lines = []
    lines.append('#include "compositor/protocol/wayland_internal.hpp"')
    lines.append('#include "compositor/protocol/wayland_server.hpp"\n')
    lines.append("namespace wayland::server\n{")
    lines.append("")

    # Per-interface dispatch tables
    for iface in interfaces:
        if not iface.requests:
            continue
        table_name = f"dispatch_table_{iface.name}"
        lines.append(f"DispatchFn {table_name}[] {{")
        for idx, req in enumerate(iface.requests):
            comment = f"{idx} /* {req['name']} */"
            lines.append(f"    [{comment}] = [](Client* client, Object* object, [[maybe_unused]] MessageReader message) {{")
            arg_exprs = []
            for arg in req["args"]:
                parse_expr = emit_parameter_parse(iface, arg)
                arg_exprs.append(f"        auto {arg['name']} = {parse_expr};")
            if arg_exprs:
                lines.extend(arg_exprs)
            # Build call
            args = ", ".join(["client"] + [a["name"] for a in req["args"]])
            lines.append(f"        static_cast<{iface.name}*>(object)->{cpp_sanitize_identifier(req['name'])}({args});")
            lines.append("    },")
        lines.append("};\n")

    # Global dispatch_tables[] and dispatch_table_view
    lines.append("std::span<const DispatchFn> dispatch_tables[] {")
    for idx, iface in enumerate(interfaces):
        comment = f"{idx} /* {iface.name} */"
        if iface.requests:
            lines.append(f"    [{comment}] = dispatch_table_{iface.name},")
        else:
            lines.append(f"    [{comment}] = {{}},")
    lines.append("};\n")

    lines.append("const std::span<const std::span<const DispatchFn>> dispatch_table_view = dispatch_tables;")
    lines.append("")
    lines.append("} // namespace wayland::server\n")

    return "\n".join(lines)

# ------------------------------------------------------------------------------

def emit_message_definition(iface: Interface, msg: Dict[str, Any], kind: str) -> str:
    arglist = emit_message_parameters(iface, msg, kind, False, False)

    return f"void {iface.name}::{cpp_sanitize_identifier(msg['name'])}({arglist})"

def cpp_writer_for_type(arg: dict) -> str:
    t = arg["type"]

    if arg.get("enum"):
        return f"write_enum({arg['name']})"

    if t in ("int", "fd"):
        return f"write_int({arg['name']})"
    if t == "uint":
        return f"write_uint({arg['name']})"
    if t == "fixed":
        return f"write_fixed({arg['name']})"
    if t == "string":
        return f"write_string({arg['name']})"
    if t == "object" or t == "new_id":
        return f"write_object({arg['name']}, client)"
    if t == "array":
        return f"write_array({arg['name']})"

    raise RuntimeError(f"Unknown type: {t}")

def emit_event_dispatch(interfaces: list[Interface]) -> str:
    lines = []
    lines.append('#include "compositor/protocol/wayland_internal.hpp"')
    lines.append('#include "compositor/protocol/wayland_server.hpp"\n')
    lines.append("namespace wayland::server\n{")
    lines.append("")

    # for iface in interfaces:
    #     if not iface.events:
    #         continue
    #     for event in iface.events:
    #         lines.append(emit_message_definition(iface, event, "event") + " {}")
    #     lines.append("")

    for iface in interfaces:
        for idx, event in enumerate(iface.events):
            # emit function signature using existing helper
            lines.append(f"{emit_message_definition(iface, event, kind="event")}")
            lines.append("{")
            lines.append("    Message msg;")
            lines.append("    for (auto[c, c_id] : _client_ids) {")
            lines.append("        if (client && c != client) continue;")
            lines.append("        MessageWriter writer{&msg};")

            # Write each event argument in order
            for arg in event["args"]:
                lines.append(f"        writer.{cpp_writer_for_type(arg)};")

            # header + send
            lines.append(f"        writer.write_header(c_id, {idx});")
            lines.append("        display_send_event(c, msg);")
            lines.append("    }")
            lines.append("}\n")

    lines.append("} // namespace wayland::server\n")

    return "\n".join(lines)

# ------------------------------------------------------------------------------

def list_wayland_protocols():
    wayland_protocols = []
    system_protocol_dir = Path("/usr/share/wayland-protocols")

    wayland_protocols.append(".build/3rdparty/wayland-protocol/protocol/wayland.xml")
    wayland_protocols.append(system_protocol_dir / "stable/xdg-shell/xdg-shell.xml")

    return wayland_protocols

def ensure_dir(path):
    os.makedirs(path, exist_ok=True)
    return Path(path)

def main():
    xml_files = list_wayland_protocols()
    target_ifaces = [
        "wl_display",
        "wl_registry",
        "wl_surface",
        "wl_buffer",
        "wl_callback",
        "wl_compositor",
        "wl_shm",
        "wl_shm_pool",
        "xdg_surface",
        "xdg_wm_base",
        "xdg_toplevel",
    ]

    generated_code_dir = ensure_dir(".build/generated")
    generated_header_dir = ensure_dir(generated_code_dir / "include")
    generated_src_dir = ensure_dir(generated_code_dir / "src")

    header_path = generated_header_dir / "compositor/protocol/wayland_server.hpp"
    ensure_dir(header_path.parent)
    dispatch_request_source_path = generated_src_dir / "wayland_dispatch_request.cpp"
    dispatch_event_source_path = generated_src_dir / "wayland_dispatch_event.cpp"

    seen_names: Dict[str, Interface] = {}
    interfaces: List[Interface] = []

    for xml_path in xml_files:
        for iface in parse_protocol_file(xml_path):
            if iface.name in seen_names:
                print(f"ERROR duplicate interface [{iface.name}] in [{xml_path}]")
            interfaces.append(iface)
            seen_names[iface.name] = interfaces

            # total_interfaces[iface.name] = iface
            # if iface.name in target_ifaces:
            #     interfaces[iface.name] = iface

    with open(header_path, "w", encoding="utf-8") as f:
        f.write(CPP_HEADER)

        for iface in interfaces:
            emit_interface_forward_enums(f, iface)

        f.write("\n")

        for iface in interfaces:
            f.write(f"struct {iface.name};\n")

        f.write("\n")

        for iface in interfaces:
            f.write(emit_interface(iface, stub = iface.name not in target_ifaces))
            f.write("\n")

        f.write(CPP_FOOTER)

    with open(dispatch_request_source_path, "w", encoding="utf-8") as f:
        f.write(emit_request_dispatch(interfaces))

    with open(dispatch_event_source_path, "w", encoding="utf-8") as f:
        f.write(emit_event_dispatch(interfaces))

if __name__ == "__main__":
    main()
