"""The game's pinned layout, so save states survive engine rebuilds
(pc/src/snapshot.c, build_boot.py's Linux link).

A save state holds the game's memory and its call stack. The stack holds
return addresses into game code and pointers into game data, so those must
not move when only the engine (the rest of pc/) is rebuilt. The link puts
them at a fixed address of their own, after the engine's sections, in this
order from 0x18000000 (each output section holds only the game's input
sections, so their addresses depend on the game's objects alone):

    rood_game_text    code of the game's units (src/ and the pc/src units
                      built like them), cd_drive.o (StGetNext waits a vblank
                      on the game's stack) and traps.o
    rood_game_rodata  their read-only data (string literals, constant tables)
    rood_state_data   the game state sections (build_boot.state_sections),
    rood_ovl_*_data   the overlays' statics, then the same for .bss
    rood_state_bss ...

The validity keys (snapshot_key.c, linked into the engine):
  - code: the pinned objects' code, read-only data, relocations and symbols
    (not their debug information), before linking: the linked bytes change
    with the engine, since game code calls engine functions;
  - layout: the symbols and sizes of the state sections of every other
    object (the host units whose statics are game state), the script, the
    pointer size and the linker.
An engine-only change keeps both; a game change makes old snapshots refused.

check(): state initialised with the address of an engine symbol would be
restored stale after an engine rebuild; those are reported."""

import hashlib
import json
import pathlib
import struct
import subprocess

# The highest end of the image: the kernel starts the heap at a random point
# up to 1 GB past it (64 bits), and the game stack is at 0x60000000.
HEAP_LIMIT = 0x60000000 - 0x40000000


def _kind(name):
    """The pinned output section an input section of a code-pinned object
    goes to, or None."""
    if name == ".text" or name.startswith(".text."):
        return "rood_game_text"
    if name == ".rodata" or name.startswith(".rodata."):
        return "rood_game_rodata"
    return None


def _is_state(name):
    return name.startswith("rood_state_") or name.startswith("rood_ovl_")


class Elf:
    """Just enough ELF (32/64-bit little endian relocatable objects)."""

    def __init__(self, path):
        self.data = data = pathlib.Path(path).read_bytes()
        if data[:4] != b"\x7fELF":
            raise ValueError(f"{path}: not ELF")
        self.is64 = data[4] == 2
        if self.is64:
            shoff, = struct.unpack_from("<Q", data, 0x28)
            shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
        else:
            shoff, = struct.unpack_from("<I", data, 0x20)
            shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
        self.sections = []
        for i in range(shnum):
            at = shoff + i * shentsize
            if self.is64:
                name, kind, flags, _, offset, size, link, info, align, entsize = struct.unpack_from(
                    "<IIQQQQIIQQ", data, at)
            else:
                name, kind, flags, _, offset, size, link, info, align, entsize = struct.unpack_from(
                    "<IIIIIIIIII", data, at)
            self.sections.append({"name": name, "type": kind, "flags": flags, "offset": offset, "size": size,
                                  "link": link, "info": info, "align": align, "entsize": entsize})
        names = self.sections[shstrndx]
        for s in self.sections:
            s["name"] = self._string(names, s["name"])
        self.symbols = []
        for s in self.sections:
            if s["type"] == 2:  # SHT_SYMTAB
                strings = self.sections[s["link"]]
                size = 24 if self.is64 else 16
                for at in range(s["offset"], s["offset"] + s["size"], size):
                    if self.is64:
                        name, info, other, shndx, value, sz = struct.unpack_from("<IBBHQQ", data, at)
                    else:
                        name, value, sz, info, other, shndx = struct.unpack_from("<IIIBBH", data, at)
                    self.symbols.append((self._string(strings, name), info, shndx, value, sz))

    def _string(self, table, offset):
        start = table["offset"] + offset
        return self.data[start:self.data.index(b"\0", start)].decode()

    def contents(self, s):
        return b"" if s["type"] == 8 else self.data[s["offset"]:s["offset"] + s["size"]]  # SHT_NOBITS

    def section_name(self, index):
        return self.sections[index]["name"] if 0 < index < len(self.sections) else None

    def relocations(self, s):
        """(offset, type, addend or None, symbol index) of a SHT_RELA/SHT_REL section."""
        rela = s["type"] == 4
        if self.is64:
            size = 24 if rela else 16
            for at in range(s["offset"], s["offset"] + s["size"], size):
                offset, info = struct.unpack_from("<QQ", self.data, at)
                addend = struct.unpack_from("<q", self.data, at + 16)[0] if rela else None
                yield offset, info & 0xFFFFFFFF, addend, info >> 32
        else:
            size = 12 if rela else 8
            for at in range(s["offset"], s["offset"] + s["size"], size):
                offset, info = struct.unpack_from("<II", self.data, at)
                addend = struct.unpack_from("<i", self.data, at + 8)[0] if rela else None
                yield offset, info & 0xFF, addend, info >> 8

    def symbol_target(self, index):
        """A relocation's target: ("section", name) or ("symbol", name, defined section or None)."""
        name, info, shndx, _, _ = self.symbols[index]
        if info & 0xF == 3:  # STT_SECTION
            return ("section", self.section_name(shndx))
        return ("symbol", name, self.section_name(shndx) if 0 < shndx < 0xFF00 else None)


def _info(path, pinned):
    """What the keys and the check need from one object, cached by content."""
    elf = Elf(path)
    h = hashlib.sha256()
    defined = {}  # global symbol -> its section stays put across engine rebuilds
    state_refs = []  # (state section, target) of each relocation in a state section
    for name, info, shndx, value, size in elf.symbols:
        section = elf.section_name(shndx) if 0 < shndx < 0xFF00 else None
        if section and info >> 4 in (1, 2):  # STB_GLOBAL, STB_WEAK
            defined[name] = bool(_is_state(section) or (pinned and _kind(section)))
    if pinned:
        # The code-pinned object: its code and data as linked, without the
        # debug information (a moved line in a shared header changes only that).
        keep = [s for s in elf.sections if not s["name"].startswith((".debug", ".rela.debug", ".rel.debug",
                                                                   ".comment", ".note", ".eh_frame",
                                                                   ".rela.eh_frame", ".rel.eh_frame"))
                and s["type"] not in (0, 2, 3)]  # NULL, SYMTAB, STRTAB
        for s in keep:
            h.update(f"{s['name']}\0{s['type']}\0{s['flags']}\0{s['size']}\0{s['align']}\0{s['entsize']}\n".encode())
            if s["type"] in (4, 9):
                target = elf.section_name(s["info"])
                for offset, kind, addend, symbol in elf.relocations(s):
                    h.update(f"{target}\0{offset}\0{kind}\0{addend}\0{elf.symbol_target(symbol)}\n".encode())
            else:
                h.update(elf.contents(s))
        for name, info, shndx, value, size in elf.symbols:
            section = elf.section_name(shndx) if 0 < shndx < 0xFF00 else shndx
            if isinstance(section, str) and section.startswith(".debug"):
                continue
            h.update(f"{name}\0{info}\0{section}\0{value}\0{size}\n".encode())
    else:
        # A host unit: only the layout of its state sections.
        for s in elf.sections:
            if _is_state(s["name"]):
                h.update(f"{s['name']}\0{s['type']}\0{s['size']}\0{s['align']}\n".encode())
        for name, info, shndx, value, size in elf.symbols:
            section = elf.section_name(shndx) if 0 < shndx < 0xFF00 else None
            if section and _is_state(section):
                h.update(f"{name}\0{info}\0{section}\0{value}\0{size}\n".encode())
    for s in elf.sections:
        if s["type"] in (4, 9) and _is_state(elf.section_name(s["info"]) or ""):
            for _, _, _, symbol in elf.relocations(s):
                state_refs.append((elf.section_name(s["info"]), elf.symbol_target(symbol)))
    # Targets resolved here where they can be: a section of this object.
    refs = []
    for state, target in state_refs:
        if target[0] == "section":
            section = target[1] or ""
            if _is_state(section) or (pinned and _kind(section)) or not section.startswith((".text", ".rodata", ".data", ".bss")):
                continue
            refs.append([state, "section " + section])
        elif target[2] is not None:
            section = target[2]
            if _is_state(section) or (pinned and _kind(section)):
                continue
            refs.append([state, target[1]])
        else:
            refs.append([state, "?" + target[1]])  # resolved against the other objects below
    sections = sorted({s["name"] for s in elf.sections if _is_state(s["name"])})
    return {"digest": h.hexdigest(), "defined": defined, "refs": refs, "sections": sections}


def script(base, pinned, state_sections):
    """The linker script (inserted after .bss) that pins the game's sections."""
    files = "\n".join(f'    KEEP("{o}"(.text .text.*))' for o in pinned)
    rodata = "\n".join(f'    KEEP("{o}"(.rodata .rodata.*))' for o in pinned)
    lines = ["/* Generated by build_boot.py (pc/tools/snapshot_layout.py): the game's code and",
             "   state at a fixed address, so an engine-only rebuild keeps save states valid. */",
             "SECTIONS", "{", f"  . = {base:#x};",
             f"  rood_game_text : {{\n    PROVIDE(__start_rood_game_text = .);\n{files}\n    PROVIDE(__stop_rood_game_text = .);\n  }}",
             "  . = ALIGN(0x200000);",
             f"  rood_game_rodata : {{\n{rodata}\n  }}",
             "  . = ALIGN(0x200000);"]
    for kind in ("data", "bss"):
        names = [f"rood_state_{kind}"] + sorted(n for n in state_sections
                                                if n.startswith("rood_ovl_") and n.endswith("_" + kind))
        for name in names:
            lines.append(f"  {name} : {{ PROVIDE(__start_{name} = .); KEEP(*({name})) PROVIDE(__stop_{name} = .); }}")
        if kind == "data":
            lines.append("  . = ALIGN(0x1000);")
    # Linux puts the heap (brk) up to 1 GB past the image's end (x86-64 ASLR):
    # it must never reach the game stack (boot_runtime.c, 0x60000000).
    lines += ["  rood_game_end = .;",
              f'  ASSERT(rood_game_end <= {HEAP_LIMIT:#x}, "the game region grew past {HEAP_LIMIT:#x}: the heap could '
              f'start on the game stack (snapshot_layout.py)")',
              "}", "INSERT AFTER .bss;", ""]
    return "\n".join(lines)


def generate(objects, pinned, base, pointer_size, out, log=print):
    """Write out/snapshot_pin.ld and out/snapshot_key.c; return their paths.
    objects: every linked object; pinned: those whose code is pinned."""
    out = pathlib.Path(out)
    cache_path = out / "snapshot_layout_cache.json"
    try:
        cache = json.loads(cache_path.read_text())
    except (OSError, ValueError):
        cache = {}
    fresh = {}
    pinned_set = set(map(str, pinned))
    infos = {}
    for obj in objects:
        data = pathlib.Path(obj).read_bytes()
        is_pinned = str(obj) in pinned_set
        key = hashlib.sha256(data).hexdigest() + ("P" if is_pinned else "H")
        info = cache.get(key) or _info(obj, is_pinned)
        fresh[key] = info
        infos[str(obj)] = info
    cache_path.write_text(json.dumps(fresh))
    text = script(base, [str(o) for o in pinned], {n for info in infos.values() for n in info["sections"]})
    linker = subprocess.run(["ld", "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    code, layout = hashlib.sha256(), hashlib.sha256()
    for obj in objects:
        info = infos[str(obj)]
        if str(obj) in pinned_set:
            code.update(f"{pathlib.Path(obj).name}\0{info['digest']}\n".encode())
        else:
            layout.update(f"{pathlib.Path(obj).name}\0{info['digest']}\n".encode())
    layout.update(f"{text}\0{pointer_size}\0{linker}\0{code.hexdigest()}".encode())
    # State initialised with the address of something that moves with the engine.
    defined = {}
    for info in infos.values():
        for name, stays in info["defined"].items():
            defined[name] = defined.get(name, False) or stays
    stale = []
    for obj, info in infos.items():
        for state, target in info["refs"]:
            if target.startswith("?"):
                name = target[1:]
                if name not in defined or defined[name]:
                    continue  # guest data, a library, or pinned
                target = name
            stale.append(f"{pathlib.Path(obj).name}: {state} -> {target}")
    (out / "snapshot_stale_refs.txt").write_text("".join(line + "\n" for line in sorted(stale)))
    if stale:
        log(f"Save states: {len(stale)} state initialisers point at engine code or data (they are restored "
            f"as saved, stale after an engine rebuild): {out / 'snapshot_stale_refs.txt'}")
    script_path = out / "snapshot_pin.ld"
    if not script_path.exists() or script_path.read_text() != text:
        script_path.write_text(text)
    key_path = out / "snapshot_key.c"
    key = ("/* Generated by build_boot.py (pc/tools/snapshot_layout.py): what a save state needs\n"
           "   of this build (pc/src/snapshot.c). */\n"
           f"const unsigned long long pc_snapshot_code_key = 0x{code.hexdigest()[:16]}ull;\n"
           f"const unsigned long long pc_snapshot_layout_key = 0x{layout.hexdigest()[:16]}ull;\n")
    if not key_path.exists() or key_path.read_text() != key:
        key_path.write_text(key)
    return script_path, key_path
