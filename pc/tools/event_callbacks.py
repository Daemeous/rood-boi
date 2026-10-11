"""Rehome the original event opcode table without executing guest code."""

import re
import struct


def generate_event_callbacks(overlay, symbol_text, source, header):
    symbols = {}
    for line in symbol_text.splitlines():
        fields = line.split()
        if len(fields) >= 8 and fields[3] == "FUNC":
            symbols[int(fields[1], 16)] = fields[7]
    targets = struct.unpack_from("<256I", overlay, 0x800E9D30 - 0x80068800)
    # Restrict targets to integer event handlers defined in this translation unit.
    source = re.sub(r"__attribute__\s*\(\(unused\)\)", "", source)
    signatures = dict(re.findall(r"\bint\s+(\w+)\s*\(([^)]*)\)\s*\{", source))
    wrappers = {}
    entries = []
    for opcode, target in enumerate(targets):
        name = symbols.get(target)
        if name not in signatures:
            raise ValueError(
                f"Unknown event handler for opcode {opcode:#x}: {target:#x}"
            )
        args = signatures[name]
        args = re.sub(r"__attribute__\s*\(\(unused\)\)", "", args)
        # Existing source places attributes after parameters; remove before matching.
        params = [p.strip() for p in args.split(",")]
        if params == ["void"]:
            call = f"{name}()"
        elif len(params) == 1 and re.fullmatch(r"u_char\s*\*\s*\w*", params[0]):
            call = f"{name}(script)"
        elif (
            len(params) == 2
            and re.fullmatch(r"u_char\s*\*\s*\w*", params[0])
            and re.fullmatch(r"short(?:\s+\w+)?", params[1])
        ):
            entries.append(name)
            continue
        else:
            raise ValueError(f"Unsupported event handler signature: {name}({args})")
        wrapper = f"pc_event_{name}"
        wrappers[wrapper] = (
            f"static int {wrapper}(u_char* script, short slot) {{ (void)script; (void)slot; return {call}; }}"
        )
        entries.append(wrapper)
    return (
        "/* Generated from the local reference overlay. Do not edit. */\n"
        + "\n".join(wrappers.values())
        + "\nint (*_opcodeFunctionTable[256])(u_char*, short) = {\n"
        + "".join(
            f"    {name}, /* {opcode:#04x} */\n" for opcode, name in enumerate(entries)
        )
        + "};\n"
    )


def generate_effect_callbacks(overlay, symbol_text, source):
    """Rehome the 49 shared effect commands and the built-in trail renderer."""
    symbols = {}
    for line in symbol_text.splitlines():
        fields = line.split()
        if len(fields) >= 8 and fields[3] == "FUNC":
            symbols[int(fields[1], 16)] = fields[7]
    source = re.sub(r"__attribute__\s*\(\(unused\)\)", "", source)
    signatures = dict(re.findall(r"\bint\s+(\w+)\s*\(([^)]*)\)\s*\{", source))
    targets = struct.unpack_from("<49I", overlay, 0x800EC3F4 - 0x80068800)
    wrappers = {}
    entries = []
    for target in targets:
        name = symbols.get(target)
        if name not in signatures or not re.fullmatch(
            r"D_800F53B8_t\s*\*\s*\w+\s*", signatures[name]
        ):
            raise ValueError(f"Unknown or incompatible effect command: {target:#x}")
        wrapper = f"pc_effect_{name}"
        wrappers[wrapper] = (
            f"static int {wrapper}(void* node) {{ return {name}(node); }}"
        )
        entries.append(wrapper)
    (renderer,) = struct.unpack_from("<I", overlay, 0x800EC324 - 0x80068800)
    if symbols.get(renderer) != "func_800CE174":
        raise ValueError("Built-in effect renderer differs from native mapping")
    return (
        "/* Generated from the local reference overlay. Do not edit. */\n"
        + "\n".join(wrappers.values())
        + "\nint (*D_800EC3F4[49])(void*) = {\n"
        + "".join(f"    {name},\n" for name in entries)
        + "};\neffectExec D_800EC324[1] = { func_800CE174 };\n"
    )
