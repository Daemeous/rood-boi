"""The boundary between the host executable and the engine library
(build_boot.py --engine-library; pc/docs/plans/2026-10-19-engine-reload.md).

The host (rood_host) is the game and everything whose state is the game's;
the engine (librood_engine.so) is the rest of pc/, which the host reloads in
place. From the objects of each side this writes:

  engine_boundary.S   (host) a trampoline per engine function the host
                      calls (`jmp *slot`; engine_host.c fills the slots
                      with dlsym at every load), and the storage of the
                      engine's variables the host reads or writes: they
                      become host-owned (the library's own references bind
                      to them: ELF interposition), so they survive a reload
  host_exports.list   the executable's --dynamic-list: what the library
                      takes from the host (host functions and data, guest
                      data imports, SDL and NRD from their archives, the
                      host-owned variables)
  host_undefined.txt  the archive symbols only the library uses (linked into
                      the host with --undefined)
NRD is linked into the library itself: its archive is built position-
independent but with hidden visibility, so the host could not export it.
  engine.map          the library's version script: it exports the
                      functions the host calls, the host-owned variables
                      and its entry points, nothing else
  engine_boundary.json  all of the above as data (engine_lib.py checks a
                      rebuilt library against it)

Refused (RuntimeError): a name both sides define (other than identical C++
inline copies), a host reference to an engine symbol that is neither code
nor data, an engine reference nothing provides."""

import json
import pathlib
import re

from snapshot_layout import Elf

# The library's entry points (pc/src/engine_reload.c), called by engine_host.c.
ENTRY_POINTS = ["rood_engine_unload", "rood_engine_resume", "rood_engine_abi", "rood_engine_note"]
# Provided by the linker or the C runtime, never by an object. Those of the
# executable's layout the library means the host's (the RPC reads host data
# between __data_start and _end): exported.
LINKER = {"_GLOBAL_OFFSET_TABLE_", "__dso_handle", "_DYNAMIC", "__ehdr_start", "__TMC_END__"}
HOST_LAYOUT = {"__executable_start", "etext", "_end", "__data_start", "__bss_start", "_edata"}
FUNCTION_TYPES = set("TWi")
DATA_TYPES = set("DBRVGSC")
NAME = re.compile(r"[A-Za-z_.$][A-Za-z0-9_.$]*")


def symbols(nm_output):
    """(defined {name: type}, undefined {name: weak}) of one object's `nm` output."""
    defined, undefined = {}, {}
    for line in nm_output.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] in ("U", "w", "v"):  # no address: undefined (w, v: weak)
            undefined[parts[1]] = parts[0] != "U"
        elif len(parts) == 3 and (parts[1].isupper() or parts[1] in ("i", "u")):  # global definitions
            defined[parts[2]] = parts[1]
    return defined, undefined


def _data_layout(obj, name):
    """(size, alignment) of a defined variable in an object."""
    elf = Elf(obj)
    for sym, info, shndx, value, size in elf.symbols:
        if sym == name and shndx != 0 and info >> 4 in (1, 2):
            if shndx == 0xFFF2:  # SHN_COMMON: the value is the alignment
                return size, max(value, 1)
            return size, max(elf.sections[shndx]["align"], 1)
    raise RuntimeError(f"{obj}: no definition of {name}")


def generate(host_objects, engine_objects, nm, archives, imports, shared, out, own_archives=()):
    """Write the boundary files into `out`; return the boundary as a dict.
    nm(path) -> `nm` output; archives: {path: `nm --defined-only` output} of
    the static libraries linked into the host; imports: the guest data
    imports' names (imports.ld); shared: names the shared libraries both
    sides link against define (libc, libstdc++, libm, libgcc_s);
    own_archives: `nm --defined-only` outputs of the static libraries linked
    into the library itself."""
    out = pathlib.Path(out)
    host_def, host_undef, engine_def, engine_undef, engine_owner = {}, {}, {}, {}, {}
    for side_def, side_undef, objects in ((host_def, host_undef, host_objects),
                                          (engine_def, engine_undef, engine_objects)):
        for obj in objects:
            defined, undefined = symbols(nm(obj))
            for name, kind in defined.items():
                if side_def is engine_def:
                    engine_owner.setdefault(name, obj)
                side_def.setdefault(name, kind)
            for name, weak in undefined.items():
                side_undef[name] = side_undef.get(name, True) and weak
    archive_def = {}
    for path, text in archives.items():
        for name in symbols(text)[0]:
            archive_def.setdefault(name, path)

    # A name defined on both sides would be two things after the split (in
    # one executable it was one). C++ inline functions and template
    # instances are weak on both sides and identical: each side keeps its own.
    both = sorted(n for n in set(host_def) & set(engine_def)
                  if not (host_def[n] in "WVu" and engine_def[n] in "WVu"))
    if both:
        raise RuntimeError("defined by the host and by the engine library: " + ", ".join(both[:40]))

    functions, data = [], {}
    for name in sorted(host_undef):
        if name in host_def or name not in engine_def:
            continue
        kind = engine_def[name]
        if kind in FUNCTION_TYPES:
            functions.append(name)
        elif kind in DATA_TYPES:
            size, align = _data_layout(engine_owner[name], name)
            data[name] = {"size": size, "align": align, "constant": kind == "R"}
        else:
            raise RuntimeError(f"the host uses {name}, a {kind!r} symbol of the engine library")

    own = set()
    for text in own_archives:
        own |= set(symbols(text)[0])
    needed = {n for n in engine_undef if n not in engine_def and n not in own}
    exports = sorted((needed & (set(host_def) | set(imports) | HOST_LAYOUT)) | set(data))
    from_archives = sorted(n for n in needed - set(host_def) if n in archive_def)
    exports = sorted(set(exports) | set(from_archives))
    unresolved = sorted(n for n in needed - set(exports) - set(shared) - LINKER if not engine_undef[n])
    if unresolved:
        raise RuntimeError("the engine library uses names nothing provides: " + ", ".join(unresolved[:40]))
    bad = [n for n in exports + functions + list(data) if not NAME.fullmatch(n)]
    if bad:
        raise RuntimeError("names the boundary cannot spell: " + ", ".join(bad[:20]))

    # The host's side: trampolines, slots, the host-owned variables.
    asm = ["/* Generated by build_boot.py --engine-library (pc/tools/engine_boundary.py): the",
           "   engine functions the host calls, through slots engine_host.c fills, and the",
           "   engine variables the host owns. */",
           '\t.section .text.rood_engine_trampolines,"ax",@progbits']
    for i, name in enumerate(functions):
        asm += [f"\t.globl {name}", f"\t.type {name}, @function", f"{name}:",
                f"\tmovl ${i}, %r11d", f"\tjmp *rood_engine_slots+{8 * i}(%rip)", f"\t.size {name}, .-{name}"]
    asm += ["/* An unbound slot: the trampoline's index is in r11. */",
            "\t.globl rood_engine_unbound", "\t.type rood_engine_unbound, @function", "rood_engine_unbound:",
            "\tmovl %r11d, %edi", "\tjmp rood_engine_missing", "\t.size rood_engine_unbound, .-rood_engine_unbound",
            '\t.section .data.rood_engine_slots,"aw",@progbits', "\t.balign 8",
            "\t.globl rood_engine_slots", "rood_engine_slots:"]
    asm += ["\t.quad rood_engine_unbound"] * len(functions)
    asm += ['\t.section .rodata.rood_engine_names,"a",@progbits', "\t.balign 8",
            "\t.globl rood_engine_slot_names", "rood_engine_slot_names:"]
    asm += [f"\t.quad .Lfn{i}" for i in range(len(functions))]
    asm += ["\t.globl rood_engine_slot_count", "rood_engine_slot_count:", f"\t.long {len(functions)}"]
    asm += [f'.Lfn{i}:\t.asciz "{name}"' for i, name in enumerate(functions)]
    for name, layout in data.items():
        asm += [f'\t.section .data.rood_engine_owned.{name},"aw",@progbits', f"\t.balign {layout['align']}",
                f"\t.globl {name}", f"\t.type {name}, @object", f"\t.size {name}, {layout['size']}",
                f"{name}:", f"\t.zero {max(layout['size'], 1)}"]
    # The table engine_host.c walks: name, host address, size, constant.
    asm += ['\t.section .rodata.rood_engine_names,"a",@progbits', "\t.balign 8",
            "\t.globl rood_engine_owned", "rood_engine_owned:"]
    for i, (name, layout) in enumerate(data.items()):
        asm += [f"\t.quad .Ldata{i}, {name}, {layout['size']}, {1 if layout['constant'] else 0}"]
    asm += ["\t.globl rood_engine_owned_count", "rood_engine_owned_count:", f"\t.long {len(data)}"]
    asm += [f'.Ldata{i}:\t.asciz "{name}"' for i, name in enumerate(data)]
    asm += ['\t.section .note.GNU-stack,"",@progbits', ""]
    files = {
        "engine_boundary.S": "\n".join(asm),
        "host_exports.list": "{\n" + "".join(f"  {n};\n" for n in exports) + "};\n",
        "host_undefined.txt": "".join(n + "\n" for n in from_archives),
        "engine.map": "{\n  global:\n" + "".join(f"    {n};\n" for n in sorted(set(functions) | set(data) | set(ENTRY_POINTS)))
                      + "  local: *;\n};\n",
    }
    boundary = {"functions": functions, "data": data, "exports": exports, "from_archives": from_archives}
    files["engine_boundary.json"] = json.dumps(boundary, indent=1, sort_keys=True) + "\n"
    for name, text in files.items():
        path = out / name
        if not path.exists() or path.read_text() != text:
            path.write_text(text)
    return boundary
