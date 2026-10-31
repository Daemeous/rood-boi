//! Removes one entry from the table at D_800F1BAC: later entries shift down and the last one is cleared (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

// sizeof(*D_800F1BAC)[0]: the views give D_800F1BAC only as an opaque pointer,
// so this is the element size the C object uses (24 bytes), as the checker's
// argument addresses show.
const elem_size = 24;

/// Byte pointer to the table's base, read from the global each time as the C does.
inline fn entries() [*]u8 {
    return @ptrCast(v.g.D_800F1BAC().*.?);
}

/// orig: func_80096444 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Removes table entry `arg0`: shifts the entries after it down one and clears the last.
pub fn func_80096444(arg0: c_int) callconv(.c) void {
    const count = v.g.D_800F227E();
    count.* = psx.conv(i16, @as(c_int, count.*) -% 1);

    if (arg0 != @as(c_int, v.g.D_800F227E().*)) {
        var i: c_int = arg0;
        while (i < @as(c_int, v.g.D_800F227E().*)) : (i +%= 1) {
            const base = entries();
            const dst = psx.byteAt(u8, base, @as(isize, i) * elem_size);
            const src = psx.byteAt(u8, base, @as(isize, i +% 1) * elem_size);
            c.vs_main_memcpy(dst, src, @as(c_uint, elem_size));
        }
    }

    const base = entries();
    const last = psx.byteAt(u8, base, @as(isize, v.g.D_800F227E().*) * elem_size);
    c.vs_main_bzero(last, @as(c_int, elem_size));
}
