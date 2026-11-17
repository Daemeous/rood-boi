#!/usr/bin/env python3
"""The layout table: every game symbol with its type, size and field offsets, from the DWARF of a linked binary
(memory lane S2, pc/docs/plans/2026-11-16-memory-lane.md 2.7).

Why: tools and tests read game memory by name, never by address. The game answers the RPC methods `field {path}` and
`symbol_info {name}` from this table (pc/src/dev_layout.c reads the sidecar the build writes next to the binary), so a
step that moves or reshapes state (S6, S7) changes the table and nothing that asks by name. For a build older than the
RPC (a comparison baseline) rood_rpc.Game answers the same questions client side from the same table, generated
from that binary's own DWARF.

    python3 pc/tools/layout_table.py BINARY [OUT]    write the sidecar (default BINARY.layout)
    python3 pc/tools/layout_table.py --diff OLD NEW  the symbols whose address or size differ
    python3 pc/tools/layout_table.py BINARY --path 'vs_battle_actors[3].unk0.facing'   resolve one path statically

The path grammar (shared with dev_layout.c): SYMBOL, then any of `.member`, `->member` (a pointer is followed by
either), `[index]`; members of anonymous structs and unions are reached directly. A name that is not found is
tried through pc/names (the decomp's name for ours and back, for symbols and members), so a path written for the
new names reads an older build, and the other way round.

Sidecar format (tab separated, one record per line):
    build  <binary size> <name>
    type   <id> <kind> <size> <name> <extra>      kinds: s struct, u union, i signed, n unsigned, f float, b bool,
                                                  p pointer (extra: target id or -), P guest pointer (psx_ptr;
                                                  extra: target id), a array (extra: "elem count"), v void
    fld    <type id> <name> <byte offset> <type id> <bit size> <bit offset>   (bit size 0: not a bit field; the bit
                                                  offset counts from the start of the struct; name - is anonymous)
    sym    <name> <address> <size> <type id or -> <d data, f function>
    alias  <old> <new>                            symbol names (pc/names/symbols.tsv)
    falias <struct name> <old> <new>              member names (pc/names/fields.tsv)
"""
import argparse
import os
import pathlib
import re
import struct
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]

# ------------------------------------------------------------------ ELF
SHF_COMPRESSED = 0x800


def elf_sections(data):
    if data[:4] != b"\x7fELF" or data[4] != 2:
        raise ValueError("not a 64-bit ELF file")
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    raw = []
    for i in range(shnum):
        name, kind, flags, addr, off, size, link, info, align, entsize = struct.unpack_from(
            "<IIQQQQIIQQ", data, shoff + i * shentsize)
        raw.append((name, kind, flags, addr, off, size, link, info, entsize))
    names_off = raw[shstrndx][4]
    sections = {}
    for name, kind, flags, addr, off, size, link, info, entsize in raw:
        end = data.index(b"\0", names_off + name)
        sections[data[names_off + name:end].decode()] = (kind, flags, addr, off, size, link, entsize)
    return sections, raw


def section_names(data, raw):
    """Section names by index."""
    shstrndx, = struct.unpack_from("<H", data, 0x3E)
    base = raw[shstrndx][4]
    return [data[base + r[0]:data.index(b"\0", base + r[0])].decode() for r in raw]


def section_bytes(data, sections, name):
    if name not in sections:
        return b""
    kind, flags, addr, off, size, link, entsize = sections[name]
    blob = data[off:off + size]
    if flags & SHF_COMPRESSED:
        ctype, _, usize, _ = struct.unpack_from("<IIQQ", blob, 0)
        if ctype != 1:
            raise ValueError("unsupported section compression")
        blob = zlib.decompress(blob[24:])
    return blob


def elf_symbols(data, sections, raw):
    """[(name, address, size, kind 'd'|'f', binding, section name)] of the symbol table's defined objects and functions."""
    kind, flags, addr, off, size, link, entsize = sections[".symtab"]
    stroff = raw[link][4]
    names = section_names(data, raw)
    out = []
    for i in range(size // 24):
        st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from("<IBBHQQ", data, off + i * 24)
        kind_code = st_info & 0xF
        if st_shndx == 0 or kind_code not in (0, 1, 2) or not st_name:
            continue  # undefined; a section or file symbol
        end = data.index(b"\0", stroff + st_name)
        out.append((data[stroff + st_name:end].decode(), st_value, st_size, "f" if kind_code == 2 else "d",
                    st_info >> 4, names[st_shndx] if st_shndx < len(names) else ""))
    return out


# ------------------------------------------------------------------ DWARF
TAGS = {
    0x01: "array", 0x04: "enum", 0x0d: "member", 0x0f: "pointer", 0x11: "cu", 0x13: "struct", 0x15: "subroutine_type",
    0x16: "typedef", 0x17: "union", 0x21: "subrange", 0x24: "base", 0x26: "const", 0x28: "enumerator",
    0x34: "variable", 0x35: "volatile", 0x37: "restrict", 0x47: "atomic",
}
AT = {
    0x01: "sibling", 0x02: "location", 0x03: "name", 0x0b: "byte_size", 0x0d: "bit_size", 0x1c: "const_value",
    0x2f: "upper_bound", 0x37: "count", 0x38: "data_member_location", 0x3c: "declaration", 0x47: "specification",
    0x49: "type", 0x3e: "encoding", 0x6b: "data_bit_offset", 0x0c: "bit_offset",
}
SUBPROGRAM = 0x2e


def uleb(buf, pos):
    result = shift = 0
    while True:
        b = buf[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if not b & 0x80:
            return result, pos
        shift += 7


def sleb(buf, pos):
    result = shift = 0
    while True:
        b = buf[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            if b & 0x40:
                result -= 1 << shift
            return result, pos


def parse_abbrevs(buf, offset):
    table, pos = {}, offset
    while True:
        code, pos = uleb(buf, pos)
        if code == 0:
            return table
        tag, pos = uleb(buf, pos)
        children = buf[pos]
        pos += 1
        attrs = []
        while True:
            at, pos = uleb(buf, pos)
            form, pos = uleb(buf, pos)
            implicit = None
            if form == 0x21:  # implicit_const
                implicit, pos = sleb(buf, pos)
            if at == 0 and form == 0:
                break
            attrs.append((at, form, implicit))
        table[code] = (tag, children, attrs)


class Dwarf:
    """The type and variable DIEs of every unit: self.dies[offset] = (tag name, {attribute: value}, [child offsets])."""

    def __init__(self, info, abbrev, strs, line_strs):
        self.dies = {}
        self.variables = []  # offsets of the file-scope variable DIEs
        self.info, self.abbrev, self.strs, self.line_strs = info, abbrev, strs, line_strs
        self.abbrev_cache = {}
        self.parse()

    def parse(self):
        info, pos = self.info, 0
        while pos < len(info):
            length, = struct.unpack_from("<I", info, pos)
            cu_start, end = pos, pos + 4 + length
            version, = struct.unpack_from("<H", info, pos + 4)
            if version >= 5:
                unit_type, addr_size, abbrev_off = struct.unpack_from("<BBI", info, pos + 6)
                body = pos + 12
                if unit_type in (4, 5):
                    body += 8
                elif unit_type in (2, 6):
                    body += 12
            else:
                abbrev_off, addr_size = struct.unpack_from("<IB", info, pos + 6)
                body = pos + 11
            abbrevs = self.abbrev_cache.get(abbrev_off)
            if abbrevs is None:
                abbrevs = self.abbrev_cache[abbrev_off] = parse_abbrevs(self.abbrev, abbrev_off)
            self.parse_unit(cu_start, body, end, abbrevs, addr_size)
            pos = end

    def parse_unit(self, cu_start, pos, end, abbrevs, addr_size):
        info, dies = self.info, self.dies
        unpack = struct.unpack_from
        stack = []  # the child lists of the open parents (None: a parent we do not record)
        while pos < end:
            die_off = pos
            code, pos = uleb(info, pos)
            if code == 0:
                if stack:
                    stack.pop()
                continue
            tag, has_children, spec = abbrevs[code]
            attrs = {}
            for at, form, implicit in spec:
                value = None
                if form == 0x0b:  # data1
                    value = info[pos]
                    pos += 1
                elif form == 0x05:  # data2
                    value, = unpack("<H", info, pos)
                    pos += 2
                elif form == 0x06:  # data4
                    value, = unpack("<I", info, pos)
                    pos += 4
                elif form == 0x07:  # data8
                    value, = unpack("<Q", info, pos)
                    pos += 8
                elif form == 0x0f:  # udata
                    value, pos = uleb(info, pos)
                elif form == 0x0d:  # sdata
                    value, pos = sleb(info, pos)
                elif form == 0x0e:  # strp
                    value = ("s", unpack("<I", info, pos)[0])
                    pos += 4
                elif form == 0x1f:  # line_strp
                    value = ("l", unpack("<I", info, pos)[0])
                    pos += 4
                elif form == 0x08:  # string
                    stop = info.index(b"\0", pos)
                    value = info[pos:stop].decode("utf-8", "replace")
                    pos = stop + 1
                elif form == 0x13:  # ref4
                    value = cu_start + unpack("<I", info, pos)[0]
                    pos += 4
                elif form == 0x11:  # ref1
                    value = cu_start + info[pos]
                    pos += 1
                elif form == 0x12:  # ref2
                    value = cu_start + unpack("<H", info, pos)[0]
                    pos += 2
                elif form == 0x14:  # ref8
                    value = cu_start + unpack("<Q", info, pos)[0]
                    pos += 8
                elif form == 0x15:  # ref_udata
                    value, pos = uleb(info, pos)
                    value += cu_start
                elif form == 0x10:  # ref_addr
                    value, = unpack("<I", info, pos)
                    pos += 4
                elif form == 0x17:  # sec_offset
                    pos += 4
                elif form == 0x18:  # exprloc
                    n, pos = uleb(info, pos)
                    value = info[pos:pos + n]
                    pos += n
                elif form == 0x0a:  # block1
                    n = info[pos]
                    value = info[pos + 1:pos + 1 + n]
                    pos += 1 + n
                elif form == 0x03:  # block2
                    n, = unpack("<H", info, pos)
                    value = info[pos + 2:pos + 2 + n]
                    pos += 2 + n
                elif form == 0x04:  # block4
                    n, = unpack("<I", info, pos)
                    value = info[pos + 4:pos + 4 + n]
                    pos += 4 + n
                elif form == 0x09:  # block
                    n, pos = uleb(info, pos)
                    value = info[pos:pos + n]
                    pos += n
                elif form == 0x19:  # flag_present
                    value = 1
                elif form == 0x0c:  # flag
                    value = info[pos]
                    pos += 1
                elif form == 0x21:  # implicit_const
                    value = implicit
                elif form == 0x01:  # addr
                    value, = unpack("<Q" if addr_size == 8 else "<I", info, pos)
                    pos += addr_size
                elif form == 0x1e:  # data16
                    pos += 16
                elif form == 0x20:  # ref_sig8
                    pos += 8
                elif form in (0x1a, 0x1b, 0x22, 0x23):  # strx, addrx, loclistx, rnglistx
                    _, pos = uleb(info, pos)
                elif form in (0x25, 0x29):
                    pos += 1
                elif form in (0x26, 0x2a):
                    pos += 2
                elif form in (0x27, 0x2b):
                    pos += 3
                elif form in (0x28, 0x2c):
                    pos += 4
                else:
                    raise ValueError(f"DWARF form {form:#x}")
                key = AT.get(at)
                if key is not None:
                    attrs[key] = value
            if tag == SUBPROGRAM and has_children and "sibling" in attrs:
                pos = attrs["sibling"]  # a function: its locals and blocks are not state
                continue
            name = TAGS.get(tag)
            if name is None:
                if has_children:
                    stack.append(None)
                continue
            children = [] if has_children else None
            dies[die_off] = (name, attrs, children)
            parent = stack[-1] if stack else None
            if parent is not None:
                parent.append(die_off)
            if name == "variable" and len(stack) == 1:
                self.variables.append(die_off)
            if has_children:
                stack.append(children)

    def string(self, value):
        if isinstance(value, tuple):
            kind, off = value
            buf = self.strs if kind == "s" else self.line_strs
            return buf[off:buf.index(b"\0", off)].decode("utf-8", "replace")
        return value


# ------------------------------------------------------------------ the table
def _ident(rest, pos):
    return re.compile(r"[A-Za-z_][A-Za-z_0-9]*").match(rest, pos)


_SYMBOL = re.compile(r"[A-Za-z_][A-Za-z_0-9]*(?::[A-Za-z_][A-Za-z_0-9]*|@[0-9a-f]+)?")


class Table:
    """types: id -> dict(kind, size, name, extra, fields [(name, offset, type, bitsize, bitoff)]);
    syms: name -> dict(address, size, type, kind)."""

    def __init__(self):
        self.types = {}
        self.syms = {}
        self.aliases = {}   # old symbol name -> new, and back
        self.faliases = {}  # struct name -> {old: new, new: old}
        self.build = ""

    # ---- generation
    @classmethod
    def from_binary(cls, path):
        data = pathlib.Path(path).read_bytes()
        sections, raw = elf_sections(data)
        dwarf = Dwarf(section_bytes(data, sections, ".debug_info"), section_bytes(data, sections, ".debug_abbrev"),
                      section_bytes(data, sections, ".debug_str"), section_bytes(data, sections, ".debug_line_str"))
        table = cls()
        table.build = f"{len(data)} {pathlib.Path(path).name}"
        _Builder(table, dwarf).add_symbols(elf_symbols(data, sections, raw))
        table.load_names()
        return table

    def load_names(self):
        sys.path.insert(0, str(ROOT / "pc/tools"))
        import symbol_names
        for old, new in symbol_names.load().items():
            self.aliases[old] = new
            self.aliases[new] = old
        fields = ROOT / "pc/names/fields.tsv"
        if fields.exists():
            for line in fields.read_text().splitlines():
                if line and not line.startswith("#"):
                    cols = line.split("\t")
                    both = self.faliases.setdefault(cols[0], {})
                    both[cols[1]] = cols[2]
                    both[cols[2]] = cols[1]

    # ---- the sidecar
    def reachable(self):
        """The ids of the types the symbols reach (the rest are what the builder made on the way)."""
        todo = [s["type"] for s in self.syms.values() if s["type"]]
        seen = set()
        while todo:
            tid = todo.pop()
            if tid in seen:
                continue
            seen.add(tid)
            t = self.types[tid]
            if t["kind"] in "pP" and t["extra"] != "-":
                todo.append(int(t["extra"]))
            elif t["kind"] == "a":
                todo.append(int(t["extra"].split()[0]))
            todo.extend(f[2] for f in t["fields"])
        return seen

    def write(self, path):
        lines = [f"build\t{self.build}"]
        keep = self.reachable()
        for tid in sorted(keep):
            t = self.types[tid]
            lines.append(f"type\t{tid}\t{t['kind']}\t{t['size']}\t{t['name'] or '-'}\t{t['extra']}")
        for tid in sorted(keep):
            for name, offset, ftype, bitsize, bitoff in self.types[tid]["fields"]:
                lines.append(f"fld\t{tid}\t{name or '-'}\t{offset}\t{ftype}\t{bitsize}\t{bitoff}")
        for name in sorted(self.syms):
            s = self.syms[name]
            ty = s["type"] if s["type"] is not None else "-"
            lines.append(f"sym\t{name}\t{s['address']:x}\t{s['size']}\t{ty}\t{s['kind']}")
        for old, new in sorted(self.aliases.items()):
            lines.append(f"alias\t{old}\t{new}")
        for struct_name, both in sorted(self.faliases.items()):
            for old, new in sorted(both.items()):
                lines.append(f"falias\t{struct_name}\t{old}\t{new}")
        tmp = pathlib.Path(str(path) + f".{os.getpid()}.tmp")
        tmp.write_text("\n".join(lines) + "\n")
        os.replace(tmp, path)

    @classmethod
    def read(cls, path):
        table = cls()
        for line in pathlib.Path(path).read_text().splitlines():
            c = line.split("\t")
            if c[0] == "build":
                table.build = " ".join(c[1:])
            elif c[0] == "type":
                table.types[int(c[1])] = {"kind": c[2], "size": int(c[3]), "name": None if c[4] == "-" else c[4],
                                          "extra": c[5], "fields": []}
            elif c[0] == "fld":
                table.types[int(c[1])]["fields"].append(
                    (None if c[2] == "-" else c[2], int(c[3]), int(c[4]), int(c[5]), int(c[6])))
            elif c[0] == "sym":
                table.syms[c[1]] = {"address": int(c[2], 16), "size": int(c[3]),
                                    "type": None if c[4] == "-" else int(c[4]), "kind": c[5]}
            elif c[0] == "alias":
                table.aliases[c[1]] = c[2]
            elif c[0] == "falias":
                table.faliases.setdefault(c[1], {})[c[2]] = c[3]
        return table

    @classmethod
    def for_binary(cls, path):
        """The table of `path`: its sidecar when that is newer than the binary and matches its size, else generated
        from the binary's DWARF and cached beside it."""
        path = pathlib.Path(path)
        sidecar = pathlib.Path(str(path) + ".layout")
        stat = path.stat()
        if sidecar.exists() and sidecar.stat().st_mtime >= stat.st_mtime:
            table = cls.read(sidecar)
            if table.build.split(" ")[0] == str(stat.st_size):
                return table
        table = cls.from_binary(path)
        try:
            table.write(sidecar)
        except OSError:
            pass
        return table

    # ---- resolution (the same grammar as pc/src/dev_layout.c)
    def symbol(self, name):
        s = self.syms.get(name)
        if s is None and name in self.aliases:
            s = self.syms.get(self.aliases[name])
        return s

    def member(self, tid, name):
        """(offset, type id, bitsize, bitoffset) of member `name` of struct/union `tid` (anonymous members looked
        through; a member renamed in pc/names/fields.tsv under either name), or None."""
        t = self.types[tid]
        wanted = (name, self.faliases.get(t["name"] or "", {}).get(name))
        for fname, offset, ftype, bitsize, bitoff in t["fields"]:
            if fname is not None and fname in wanted:
                return offset, ftype, bitsize, bitoff
        for fname, offset, ftype, bitsize, bitoff in t["fields"]:
            if fname is None and self.types[ftype]["kind"] in "su":
                found = self.member(ftype, name)
                if found:
                    return found[0] + offset, found[1], found[2], found[3] + (offset * 8 if found[2] else 0)
        return None

    def resolve(self, path, read=None):
        """Walk `path` -> dict(address, size, type, bitsize, bitoffset, limit). bitoffset: the shift of a bit field
        within the byte at address; limit: the bytes from address to the end of the symbol (or of the struct
        reached through a pointer: an allocation's extent is not known, so then None), the span that reads and
        writes by this name may cover. `read(address, length) -> bytes` is needed only to follow a pointer; without
        it a path through a pointer raises LookupError."""
        m = _SYMBOL.match(path, len(path) - len(path.lstrip()))
        if not m:
            raise LookupError(f"no symbol in {path!r}")
        sym = self.symbol(m.group(0))
        if sym is None:
            raise LookupError(f"no symbol {m.group(0)!r}")
        address, tid, rest = sym["address"], sym["type"], path[m.end():]
        bitsize = bitoff = 0
        if tid is None:
            if rest.strip():
                raise LookupError(f"{m.group(0)} has no type information")
            return {"address": address, "size": sym["size"], "type": None, "bitsize": 0, "bitoffset": 0,
                    "limit": sym["size"] or None}
        size = self.types[tid]["size"] or sym["size"]
        limit = max(size, sym["size"]) or None
        pos = 0
        while pos < len(rest):
            if rest[pos].isspace():
                pos += 1
                continue
            t = self.types[tid]
            if rest[pos] == "[":
                close = rest.find("]", pos)
                if close < 0:
                    raise LookupError("unclosed [")
                index = int(rest[pos + 1:close], 0)
                pos = close + 1
                if t["kind"] == "a":
                    elem, count = (int(x) for x in t["extra"].split())
                    if count and not 0 <= index < count:
                        raise LookupError(f"index {index} outside [0,{count})")
                    address += index * self.types[elem]["size"]
                    limit = None if limit is None else limit - index * self.types[elem]["size"]
                    tid = elem
                elif t["kind"] in "pP":
                    tid = self._target(t)
                    address = self._follow(address, t, read) + index * self.types[tid]["size"]
                    limit = None
                else:
                    raise LookupError("[] on a " + t["kind"])
                size = self.types[tid]["size"]
                bitsize = bitoff = 0
                continue
            arrow = rest.startswith("->", pos)
            if arrow:
                pos += 2
            elif rest[pos] == ".":
                pos += 1
            else:
                raise LookupError(f"unexpected {rest[pos:]!r}")
            m = _ident(rest, pos)
            if not m:
                raise LookupError(f"member name expected at {rest[pos:]!r}")
            pos = m.end()
            if t["kind"] in "pP":
                tid = self._target(t)
                address = self._follow(address, t, read)
                limit = None
                t = self.types[tid]
            elif arrow:
                raise LookupError("-> on a non-pointer")
            if t["kind"] not in "su":
                raise LookupError(f"member {m.group(0)} of a {t['kind']}")
            found = self.member(tid, m.group(0))
            if found is None:
                raise LookupError(f"no member {m.group(0)} in {t['name'] or 'an anonymous struct'}")
            offset, tid, bitsize, bitoff = found
            address += offset
            limit = None if limit is None else limit - offset
            bitoff = bitoff - offset * 8 if bitsize else 0  # the bit within the byte at `address`
            size = self.types[tid]["size"]
        return {"address": address, "size": size, "type": tid, "bitsize": bitsize, "bitoffset": bitoff, "limit": limit}

    KIND_WORDS = {"i": "int", "n": "uint", "f": "float", "b": "bool", "p": "pointer", "P": "guest_pointer",
                  "s": "struct", "u": "union", "a": "array", "v": "void"}

    def kind_word(self, tid):
        return self.KIND_WORDS.get(self.types[tid]["kind"], "none") if tid else "none"

    def decode(self, ref, data):
        """The value of a scalar at `ref` from its bytes (what the game's `field` answers as "value"), else None."""
        tid = ref["type"]
        if not tid:
            return None
        t = self.types[tid]
        if ref["bitsize"]:
            width = (ref["bitoffset"] + ref["bitsize"] + 7) // 8
            v = int.from_bytes(data[:width], "little") >> ref["bitoffset"] & ((1 << ref["bitsize"]) - 1)
            if t["kind"] == "i" and v >> (ref["bitsize"] - 1) & 1:
                v -= 1 << ref["bitsize"]
            return v
        size = t["size"]
        if t["kind"] == "f" and size in (4, 8):
            return struct.unpack("<f" if size == 4 else "<d", data[:size])[0]
        if t["kind"] in "su" and size in (1, 2, 4, 8):
            return int.from_bytes(data[:size], "little")  # a word of bit fields: its bits as one number
        if t["kind"] in "inbpP" and 0 < size <= 8:
            return int.from_bytes(data[:4 if t["kind"] == "P" else size], "little", signed=t["kind"] == "i")
        return None

    def info(self, ref):
        """What the game's `symbol_info` answers for the place `ref` (address, size, kind, type, count, fields)."""
        tid = ref["type"]
        out = {"address": ref["address"], "size": ref["size"], "kind": self.kind_word(tid)}
        t = self.types.get(tid)
        if t and t["name"]:
            out["type"] = t["name"]
        base = tid
        if t and t["kind"] in "apP":
            elem = int(t["extra"].split()[0]) if t["kind"] == "a" else (int(t["extra"]) if t["extra"] != "-" else 0)
            if t["kind"] == "a":
                out["count"] = int(t["extra"].split()[1])
            et = self.types.get(elem)
            if et:
                out["element" if t["kind"] == "a" else "target"] = {"size": et["size"], "kind": self.kind_word(elem),
                                                                    **({"type": et["name"]} if et["name"] else {})}
            base = elem
            while base and self.types[base]["kind"] == "a":
                base = int(self.types[base]["extra"].split()[0])
        bt = self.types.get(base)
        if bt and bt["kind"] in "su":
            out["fields"] = []
            for name, offset, ftype, bits, bitoff in bt["fields"]:
                f = {"name": name or "", "offset": offset, "size": self.types[ftype]["size"],
                     "kind": self.kind_word(ftype)}
                if self.types[ftype]["name"]:
                    f["type"] = self.types[ftype]["name"]
                if bits:
                    f.update(bits=bits, bit_offset=bitoff)
                out["fields"].append(f)
        return out

    def _target(self, t):
        if t["extra"] == "-":
            raise LookupError("a void pointer cannot be followed")
        return int(t["extra"])

    def _follow(self, address, t, read):
        if read is None:
            raise LookupError("a pointer needs a reader")
        value = int.from_bytes(read(address, 4 if t["kind"] == "P" else 8), "little")
        if value == 0:
            raise LookupError("null pointer")
        return value


class NameIndex:
    """address -> "symbol.member[3].field+0x1" (the reverse of Table.resolve), for the tools that show where memory
    differs (snapshot_diff): a symbol, then the members and array elements that cover the address."""

    def __init__(self, table):
        self.t = table
        self.rows = sorted((s["address"], s["size"], n) for n, s in table.syms.items()
                           if "@" not in n and ":" not in n and s["kind"] == "d")
        self.starts = [r[0] for r in self.rows]

    def name(self, address):
        import bisect
        i = bisect.bisect_right(self.starts, address) - 1
        for j in range(i, max(i - 64, -1), -1):
            start, size, name = self.rows[j]
            if address < start + max(size, 1):
                sym = self.t.syms[name]
                return name + (self._member(sym["type"], address - start) if sym["type"] else (
                    f"+{address - start:#x}" if address != start else ""))
        return f"{address:#x}"

    def _member(self, tid, offset):
        t = self.t.types[tid]
        if t["kind"] == "a":
            elem, _count = (int(x) for x in t["extra"].split())
            size = self.t.types[elem]["size"] or 1
            index, rest = divmod(offset, size)
            return f"[{index}]" + self._member(elem, rest)
        if t["kind"] in "su":
            best = None
            for name, off, ftype, bits, _bo in t["fields"]:
                size = self.t.types[ftype]["size"]
                if off <= offset < off + max(size, 1) and (best is None or off >= best[1]):
                    best = (name, off, ftype)
            if best:
                name, off, ftype = best
                return (f".{name}" if name else "") + self._member(ftype, offset - off)
        return f"+{offset:#x}" if offset else ""


class _Builder:
    def __init__(self, table, dwarf):
        self.t, self.d = table, dwarf
        self.memo = {}      # die offset -> type id
        self.shared = {}    # (kind, name, size) -> type id: base types, enums and named aggregates across units
        self.next_id = 1
        self.signatures = {}  # struct type id -> the member signatures merged into it
        self.void = self.new("v", 0, "void", "-")

    def new(self, kind, size, name, extra):
        tid = self.next_id
        self.next_id += 1
        self.t.types[tid] = {"kind": kind, "size": size, "name": name, "extra": extra, "fields": []}
        return tid

    def name_of(self, attrs):
        v = attrs.get("name")
        return None if v is None else self.d.string(v)

    def type_of(self, attrs):
        off = attrs.get("type")
        return self.void if off is None else self.tid(off)

    def tid(self, off):
        got = self.memo.get(off)
        if got is not None:
            return got
        entry = self.d.dies.get(off)
        if entry is None:
            return self.void
        tag, attrs, children = entry
        if tag in ("typedef", "const", "volatile", "restrict", "atomic"):
            target = self.type_of(attrs)  # transparent; a typedef names an anonymous struct or union it points at
            tt = self.t.types[target]
            if tag == "typedef" and tt["name"] is None and tt["kind"] in "su":
                tt["name"] = self.name_of(attrs)
            self.memo[off] = target
            return target
        if tag == "base":
            enc = attrs.get("encoding")
            kind = {2: "b", 3: "f", 4: "f", 5: "i", 6: "i", 7: "n", 8: "n"}.get(enc, "n")
            return self._shared(off, kind, attrs.get("byte_size", 0), self.name_of(attrs))
        if tag == "enum":
            signed = any(isinstance(self.d.dies[c][1].get("const_value"), int) and self.d.dies[c][1]["const_value"] < 0
                         for c in children or [])
            return self._shared(off, "i" if signed else "n", attrs.get("byte_size", 4), self.name_of(attrs))
        if tag == "pointer":
            tid = self.new("p", attrs.get("byte_size", 8), None, "-")
            self.memo[off] = tid  # registered before the target: a struct that points to itself
            if attrs.get("type") is not None:
                self.t.types[tid]["extra"] = str(self.tid(attrs["type"]))
            same = self.shared.setdefault(("p", self.t.types[tid]["extra"]), tid)
            self.memo[off] = same
            return same
        if tag == "array":
            tid = self.type_of(attrs)
            dims = []
            for c in children or []:
                ctag, cattrs, _ = self.d.dies[c]
                if ctag == "subrange":
                    if "count" in cattrs:
                        dims.append(cattrs["count"])
                    elif isinstance(cattrs.get("upper_bound"), int):
                        dims.append(cattrs["upper_bound"] + 1)
                    else:
                        dims.append(0)
            for count in reversed(dims or [0]):
                got = self.shared.get(("a", tid, count))
                if got is None:
                    got = self.shared[("a", tid, count)] = self.new(
                        "a", count * self.t.types[tid]["size"], None, f"{tid} {count}")
                tid = got
            self.memo[off] = tid
            return tid
        if tag in ("struct", "union"):
            return self._aggregate(off, tag, attrs, children)
        self.memo[off] = self.void
        return self.void

    def type_key(self, off):
        """A cheap identity of the type at a DIE (typedefs and qualifiers looked through): tag, name, size."""
        for _ in range(8):
            entry = self.d.dies.get(off)
            if entry is None:
                return None
            tag, attrs, _children = entry
            if tag in ("typedef", "const", "volatile", "restrict", "atomic") and attrs.get("type") is not None:
                off = attrs["type"]
                continue
            return (tag, self.name_of(attrs), attrs.get("byte_size"))
        return None

    def _shared(self, off, kind, size, name):
        key = (kind, name, size)
        got = self.shared.get(key)
        if got is None:
            got = self.shared[key] = self.new(kind, size, name, "-")
        self.memo[off] = got
        return got

    def _aggregate(self, off, tag, attrs, children):
        """A struct or union. The same struct seen by several units (the C header's, and the Zig views' of it, whose
        type names carry a module prefix and whose members may carry readable names) is one type: the members of
        every view are merged, a member name at an offset in any of them answers (pc/names has the old names too)."""
        kind = "s" if tag == "struct" else "u"
        name = self.name_of(attrs)
        base = re.sub(r"^views_[A-Za-z0-9_]+\.", "", name) if name else None
        size = attrs.get("byte_size", 0)
        key = (kind, base, size) if base else None
        declaration = bool(attrs.get("declaration"))
        sig = None
        if key is None and not declaration:
            # anonymous (a typedef names it later): one type per layout, however many units repeat it
            sig = ("anon", kind, size, tuple((self.name_of(self.d.dies[c][1]), self.d.dies[c][1].get("data_member_location"),
                                              self.d.dies[c][1].get("bit_size"), self.d.dies[c][1].get("type") and
                                              self.type_key(self.d.dies[c][1]["type"]))
                                             for c in children or [] if self.d.dies[c][0] == "member"))
            got = self.shared.get(sig)
            if got is not None:
                self.memo[off] = got
                return got
        existing = self.shared.get(key) if key else None
        if existing is None and key and declaration:
            # an incomplete type: the complete one of any size
            existing = next((v for k, v in self.shared.items() if k[:2] == (kind, base) and k[2] != 0), None)
        if existing is not None and (declaration or not children):
            self.memo[off] = existing
            return existing
        signature = None
        if existing is not None:
            signature = tuple((self.name_of(self.d.dies[c][1]), self.d.dies[c][1].get("data_member_location"),
                               self.d.dies[c][1].get("bit_size")) for c in children if self.d.dies[c][0] == "member")
            if signature in self.signatures[existing]:
                self.memo[off] = existing
                return existing
            self.signatures[existing].add(signature)
            tid = existing
        else:
            tid = self.new(kind, size, base if key else name, "-")
            if key and not declaration:
                self.shared[key] = tid
            elif sig is not None:
                self.shared[sig] = tid
            self.signatures[tid] = {tuple((self.name_of(self.d.dies[c][1]), self.d.dies[c][1].get("data_member_location"),
                                           self.d.dies[c][1].get("bit_size")) for c in children or []
                                          if self.d.dies[c][0] == "member")}
        self.memo[off] = tid
        fields = []
        for c in children or []:
            ctag, cattrs, _ = self.d.dies[c]
            if ctag != "member":
                continue
            offset = cattrs.get("data_member_location", 0)
            offset = offset if isinstance(offset, int) else 0
            bitsize, bitoff = cattrs.get("bit_size", 0), 0
            if bitsize:
                if "data_bit_offset" in cattrs:
                    bitoff = cattrs["data_bit_offset"]
                    offset = bitoff // 8
                else:
                    bitoff = offset * 8 + cattrs.get("bit_offset", 0)
            fields.append((self.name_of(cattrs), offset, self.type_of(cattrs), bitsize, bitoff))
        t = self.t.types[tid]
        if existing is not None:
            have = {(f[0], f[1], f[3]): i for i, f in enumerate(t["fields"])}
            for f in fields:
                i = have.get((f[0], f[1], f[3]))
                if i is None:
                    t["fields"].append(f)
                elif self.knowledge(f[2]) > self.knowledge(t["fields"][i][2]):
                    t["fields"][i] = f
            return tid
        t["fields"] = fields
        if kind == "s" and [f[0] for f in fields] == ["raw", "psx_type_"]:
            # a psx_ptr: a guest pointer to the target of psx_type_'s element pointer
            ptype = self.t.types[fields[1][2]]
            target = "-"
            if ptype["kind"] == "a":
                elem = self.t.types[int(ptype["extra"].split()[0])]
                if elem["kind"] == "p":
                    target = elem["extra"]
            t["kind"], t["fields"], t["extra"] = "P", [], target
        return tid

    def knowledge(self, tid, depth=0, seen=None):
        """How much the type says: pointers to structs and arrays of them say more than void* and bytes, and a struct
        whose pointer members are typed more than one whose are not. (A data import the build defines natively has
        a generic definition; the header's declaration, or another unit's view of the struct, is the type.)"""
        seen = set() if seen is None else seen
        t = self.t.types[tid]
        if depth > 4 or tid in seen:
            return 0
        seen = seen | {tid}
        if t["kind"] == "p":
            target = int(t["extra"]) if t["extra"] != "-" else 0
            return 0 if not target or self.t.types[target]["kind"] in "vin" else 1 + self.knowledge(target, depth + 1, seen)
        if t["kind"] == "a":
            return self.knowledge(int(t["extra"].split()[0]), depth + 1, seen)
        if t["kind"] in "su":
            return 2 + len(t["fields"]) + sum(self.knowledge(f[2], depth + 1, seen) for f in t["fields"]
                                              if self.t.types[f[2]]["kind"] in "pa")
        return 0

    def best_type(self, tid, declarations, size):
        """The definition's type, or a declaration's when that is the same size and says more."""
        best, score = tid, self.knowledge(tid)
        for attrs in declarations:
            candidate = self.type_of(attrs)
            ct = self.t.types[candidate]
            if ct["size"] not in (0, size) and size:
                continue
            # an array declared without a bound takes the symbol's size
            if ct["kind"] == "a" and not ct["size"] and size:
                elem = int(ct["extra"].split()[0])
                if not self.t.types[elem]["size"] or size % self.t.types[elem]["size"]:
                    continue
                if self.t.types[tid]["kind"] != "a":
                    continue
            elif ct["size"] != size and size:
                continue
            k = self.knowledge(candidate)
            if k > score:
                best, score = candidate, k
        return best

    def add_symbols(self, symbols):
        t = self.t
        holders = {}  # (name, address) -> the attributes that carry the variable's type
        declared = {}  # name -> the attributes of every declaration (extern ...; in a header) of it
        for off in self.d.variables:
            _, attrs, _ = self.d.dies[off]
            loc = attrs.get("location")
            name = self.name_of(attrs)
            spec = self.d.dies.get(attrs.get("specification"))
            if spec is not None:
                name = name or self.name_of(spec[1])
            if name is not None and "type" in attrs:
                declared.setdefault(name, []).append(attrs)
            if not isinstance(loc, (bytes, bytearray)) or len(loc) != 9 or loc[0] != 0x03:
                continue
            address, = struct.unpack_from("<Q", loc, 1)
            holder = attrs
            if spec is not None and "type" not in attrs:
                holder = spec[1]
            if name is not None:
                holders[(name, address)] = holder
        by_base = {}
        for key, found in self.shared.items():
            if key[0] in ("s", "u") and key[1]:
                by_base.setdefault(key[1], []).append((key[2], found))
        counts, last = {}, {}
        for i, (name, *_) in enumerate(symbols):
            counts[name] = counts.get(name, 0) + 1
            last[name] = i  # file-local names repeat across units: the plain name is the last one in the symbol
            # table (what nm-based lookups always answered), each is also name@address
        for i, (name, address, size, kind, binding, section) in enumerate(symbols):
            holder = holders.get((name, address))
            tid = self.type_of(holder) if holder is not None and "type" in holder else None
            if tid is None and name in declared and kind == "d":
                # no definition with a type here (a pinned import, defined by the linker script): the header's
                # declaration is the type, when it fits the symbol
                best, score = None, -1
                for attrs in declared[name]:
                    candidate = self.type_of(attrs)
                    ct = self.t.types[candidate]
                    if size and ct["size"] > size:
                        continue  # (a pinned import's extent may run past its declared type: a prefix is fine)
                    k = self.knowledge(candidate)
                    if k > score:
                        best, score = candidate, k
                tid = best
            elif tid is not None and name in declared:
                tid = self.best_type(tid, declared[name], size)
            if tid is not None:
                ty = t.types[tid]
                if ty["kind"] == "a" and not ty["size"] and size:
                    # an array declared without a bound (extern T x[]): the symbol's size gives its count
                    elem = int(ty["extra"].split()[0])
                    if t.types[elem]["size"] and size % t.types[elem]["size"] == 0:
                        tid = self.new("a", size, None, f"{elem} {size // t.types[elem]['size']}")
                size = t.types[tid]["size"] or size
            if tid is None and kind == "d" and (name + "_t") in by_base:
                # no variable of this name carries a type, but the header's typedef <name>_t of the symbol's size does
                for ct_size, candidate in by_base[name + "_t"]:
                    if ct_size and (not size or ct_size == size):
                        tid, size = candidate, ct_size
                        break
            entry = {"address": address, "size": size, "type": tid, "kind": kind}
            if last[name] == i:
                t.syms[name] = entry
            if counts[name] > 1:
                t.syms[f"{name}@{address:x}"] = entry
            if section.startswith("rood_ovl_"):
                # an overlay's own statics: other overlays have statics of the same name, so also
                # <overlay>:<name> (the overlay's section without its _bss/_data; a function's static without the .N)
                key = re.sub(r"_(?:bss|data|rodata|text)$", "", section) + ":" + name.split(".")[0]
                t.syms.setdefault(key, entry)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("binary", nargs="?")
    parser.add_argument("out", nargs="?")
    parser.add_argument("--path", help="resolve this path in the table of BINARY (static: no pointers followed)")
    parser.add_argument("--diff", nargs=2, metavar=("OLD", "NEW"))
    args = parser.parse_args()
    if args.diff:
        old, new = (Table.for_binary(p) for p in args.diff)
        for name in sorted(set(old.syms) | set(new.syms)):
            a, b = old.syms.get(name), new.syms.get(name)
            if a is None or b is None:
                print(f"{'added' if a is None else 'removed'}\t{name}")
            elif (a["address"], a["size"]) != (b["address"], b["size"]):
                print(f"moved\t{name}\t{a['address']:#x}+{a['size']} -> {b['address']:#x}+{b['size']}")
        return
    if not args.binary:
        parser.error("BINARY")
    if args.path:
        r = Table.for_binary(args.binary).resolve(args.path)
        print(f"{r['address']:#x} size {r['size']}")
        return
    table = Table.from_binary(args.binary)
    out = args.out or args.binary + ".layout"
    table.write(out)
    print(f"{out}: {len(table.syms)} symbols, {len(table.types)} types")


if __name__ == "__main__":
    main()
