#!/usr/bin/env python3

import xml.etree.ElementTree as ET
from pathlib import Path
from typing import List, Dict, Any

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
#include "wayland_core.hpp"

namespace wayland::server
{

"""

CPP_FOOTER = "} // namespace wayland::server\n"

def cpp_sanitize_identifier(name: str) -> str:
    if name[0].isdigit():
        return "_" + name

    if name in ("default"):
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
    if t == "object" or t == "new_id":
        iface = arg.get("interface")
        return f"{iface}*" if iface else "Object*"
    if t == "array":
        return "std::span<u8>"

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

def arg_parameter_name(iface: Interface, arg: Dict[str, str], stub: bool):
    name = arg['name']
    if name not in ('x', 'y', 'id'):
        type = cpp_type(iface, arg)
        if len(name) > 1 and type.find(name) != -1:
            return ""
    arg_str = f"/* {name} */" if stub else name
    return f" {arg_str}"

def emit_message_declaration(iface: Interface, msg: Dict[str, Any], kind: str, stub: bool) -> str:
    args = []
    if kind == "request":
        args.append("Client*")
    for arg in msg["args"]:
        args.append(f"{cpp_type(iface, arg)}{arg_parameter_name(iface, arg, False)}")
    arglist = ", ".join(args)

    line_end = " {}" if stub else ";"

    return f"    void {msg['name']}({arglist}){line_end}"

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
            lines.append(emit_message_declaration(iface, evt, "event", True))
    lines.append("};\n")

    return "\n".join(lines)

# ---- Request dispatch --------------------------------------------------------

def emit_parameter_parse(iface: Interface, arg: dict) -> str:
    enum = arg.get("enum")
    if enum:
        return f"message_read_enum<{get_enum_name(iface, enum)}>(message)"

    t = arg["type"]

    if t in ("int", "fd"):
        return "message_read_int(message)"
    if t == "uint":
        return "message_read_uint(message)"
    if t == "fixed":
        return "message_read_fixed(message)"
    if t == "string":
        return "message_read_string(message)"
    if t == "object":
        iface_name = arg.get("interface")
        if iface_name:
            return f"message_read_object<{iface_name}>(message, client, {iface.interface_id})"
        raise RuntimeError("Expected object type")
    if t == "new_id":
        iface = arg.get("interface")
        if iface:
            return f"message_read_new_id<{iface}>(message, client)"
        return "message_read_new_id<Object>(message, client)"
    if t == "array":
        return "message_read_array(message)"
    if t == "enum":
        enum = arg.get("enum")
        if enum:
            return f"message_read_enum<{enum}>(message)"
        return "message_read_int(message)"

    # Fallback
    return "message_read_int(message)"


def emit_request_dispatch(interfaces: list[object]) -> str:
    lines = []
    lines.append('#include "wayland_internal.hpp"')
    lines.append('#include "wayland_server.hpp"\n')
    lines.append("namespace wayland::server\n{")
    lines.append("")
    lines.append("using DispatchFn = void(*)(Client* client, Object* object, Message);")
    lines.append("")

    # Per-interface dispatch tables
    for iface in interfaces:
        if not iface.requests:
            continue
        table_name = f"dispatch_table_{iface.name}"
        lines.append(f"DispatchFn {table_name}[] {{")
        for idx, req in enumerate(iface.requests):
            comment = f"{idx} /* {req['name']} */"
            lines.append(f"    [{comment}] = [](Client* client, Object* object, [[maybe_unused]] Message message) {{")
            arg_exprs = []
            for arg in req["args"]:
                parse_expr = emit_parameter_parse(iface, arg)
                arg_exprs.append(f"        auto {arg['name']} = {parse_expr};")
            if arg_exprs:
                lines.extend(arg_exprs)
            # Build call
            args = ", ".join(["client"] + [a["name"] for a in req["args"]])
            lines.append(f"        static_cast<{iface.name}*>(object)->{req['name']}({args});")
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

def main():
    xml_files = [
        ".build/3rdparty/wayland-protocol/protocol/wayland.xml",
        "/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml"
    ]
    header_path = "src/compositor/protocol/wayland_server.hpp"
    dispatch_source_path = "src/compositor/protocol/wayland_request_dispatch.cpp"
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

    total_interfaces: Dict[str, Interface] = {}
    interfaces: Dict[str, Interface] = {}

    for xml_path in xml_files:
        for iface in parse_protocol_file(xml_path):
            total_interfaces[iface.name] = iface
            if iface.name in target_ifaces:
                interfaces[iface.name] = iface

    with open(header_path, "w", encoding="utf-8") as f:
        f.write(CPP_HEADER)

        for iface in total_interfaces.values():
            emit_interface_forward_enums(f, iface)

        f.write("\n")

        for iface in total_interfaces.values():
            f.write(f"struct {iface.name};\n")

        f.write("\n")

        for iface in total_interfaces.values():
            f.write(emit_interface(iface, stub = iface.name not in interfaces))
            f.write("\n")

        f.write(CPP_FOOTER)

    with open(dispatch_source_path, "w", encoding="utf-8") as f:
        f.write(emit_request_dispatch(total_interfaces.values()))

if __name__ == "__main__":
    main()
