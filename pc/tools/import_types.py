"""Declared types of data imports, read from an object's DWARF (run inside gdb).

    gdb -batch -nx OBJECT -ex "python import sys; sys.argv = ['x', 'in.json', 'out.json']" \
        -x pc/tools/import_types.py

in.json lists names; out.json maps each name found to its declared type:
{"type": str, "size": host sizeof (0 if incomplete), "ps1_size": the same
type's size with PS1 (mipsel ILP32) layout rules, "element": for arrays,
{"size", "ps1_size", "pointer"}, "pointer_words": the type is pointers
(through any arrays)}. build_boot.py --arch 64 uses it to find the
imports whose layout differs on LP64 (pointers and 8-byte longs): those cannot
stay at their 4-byte-per-pointer PS1 addresses.
"""
import json
import sys

import gdb  # noqa: provided by gdb


def ps1_layout(t):
    """(size, alignment) of type t under PS1 rules: 4-byte pointers and longs."""
    t = t.strip_typedefs()
    code = t.code
    if code in (gdb.TYPE_CODE_PTR, gdb.TYPE_CODE_REF):
        return 4, 4
    if code == gdb.TYPE_CODE_ARRAY:
        low, high = t.range()
        size, align = ps1_layout(t.target())
        return max(0, high - low + 1) * size, align
    if code in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
        if t.sizeof == 0:
            return 0, 1
        offset, max_align, union_size = 0, 1, 0
        packed = None
        bitfield_end = None
        for field in t.fields():
            if field.bitsize:
                # Bitfields (P_TAG's addr:24/len:8) use int-sized units on both
                # targets: keep the host's bit positions within the struct.
                end = (field.bitpos + field.bitsize + 7) // 8
                offset = max(offset, end)
                max_align = max(max_align, min(4, field.type.strip_typedefs().sizeof))
                continue
            size, align = ps1_layout(field.type)
            # psx_ptr is packed, aligned(4): its zero-length pointer array
            # adds no alignment beyond the host's.
            host_align = field.type.strip_typedefs().alignof if hasattr(field.type, "alignof") else align
            if host_align < align:
                align = host_align
            max_align = max(max_align, align)
            if code == gdb.TYPE_CODE_UNION:
                union_size = max(union_size, size)
                continue
            offset = (offset + align - 1) // align * align + size
        if code == gdb.TYPE_CODE_UNION:
            offset = union_size
        # Struct alignment: never above the host's (packed structs stay packed).
        if hasattr(t, "alignof"):
            max_align = min(max_align, t.alignof) or 1
        return (offset + max_align - 1) // max_align * max_align, max_align
    if code == gdb.TYPE_CODE_FUNC:
        return 0, 1
    size = t.sizeof
    if code == gdb.TYPE_CODE_INT and size == 8 and "long long" not in str(t):
        return 4, 4  # long / unsigned long
    if size >= 8:
        return size, 8 if code != gdb.TYPE_CODE_FLT or size == 8 else 4
    return size, max(1, size)


def describe(symbol):
    t = symbol.type
    info = {"type": str(t), "size": t.sizeof, "ps1_size": ps1_layout(t)[0]}
    stripped = t.strip_typedefs()
    if stripped.code == gdb.TYPE_CODE_ARRAY:
        element = stripped.target()
        info["element"] = {
            "size": element.sizeof,
            "ps1_size": ps1_layout(element)[0],
            "pointer": element.strip_typedefs().code == gdb.TYPE_CODE_PTR,
        }
    info["pointer"] = stripped.code == gdb.TYPE_CODE_PTR
    leaf = stripped
    while leaf.code == gdb.TYPE_CODE_ARRAY:
        leaf = leaf.target().strip_typedefs()
    # Every 4-byte PS1 word is a pointer (scalars and arrays of pointers).
    info["pointer_words"] = leaf.code == gdb.TYPE_CODE_PTR
    return info


def main():
    names = json.load(open(sys.argv[1]))
    result = {}
    for name in names:
        symbol = gdb.lookup_global_symbol(name) or gdb.lookup_static_symbol(name)
        # Extern declarations are LOC_UNRESOLVED, not "variables".
        if symbol is None or symbol.is_function or symbol.addr_class == gdb.SYMBOL_LOC_TYPEDEF:
            continue
        try:
            result[name] = describe(symbol)
        except gdb.error as error:
            result[name] = {"error": str(error)}
    json.dump(result, open(sys.argv[2], "w"))


main()
