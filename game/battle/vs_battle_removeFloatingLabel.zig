//! Removes one entry from the table at vs_battle_floatingLabels: later entries shift down and the last one is cleared (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

// sizeof(*vs_battle_floatingLabels)[0]: the views give vs_battle_floatingLabels only as an opaque pointer,
// so this is the element size the C object uses (24 bytes), as the checker's
// argument addresses show.
const elem_size = 24;

/// Byte pointer to the table's base, read from the global each time as the C does.
inline fn entries() [*]u8 {
    return @ptrCast(v.g.vs_battle_floatingLabels().*.?);
}

/// orig: vs_battle_removeFloatingLabel (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Removes table entry `arg0`: shifts the entries after it down one and clears the last.
pub fn vs_battle_removeFloatingLabel(arg0: c_int) callconv(.c) void {
    const count = v.g.vs_battle_floatingLabelCount();
    count.* = psx.conv(i16, @as(c_int, count.*) -% 1);

    if (arg0 != @as(c_int, v.g.vs_battle_floatingLabelCount().*)) {
        var i: c_int = arg0;
        while (i < @as(c_int, v.g.vs_battle_floatingLabelCount().*)) : (i +%= 1) {
            const base = entries();
            const dst = psx.byteAt(u8, base, @as(isize, i) * elem_size);
            const src = psx.byteAt(u8, base, @as(isize, i +% 1) * elem_size);
            c.vs_main_memcpy(dst, src, @as(c_uint, elem_size));
        }
    }

    const base = entries();
    const last = psx.byteAt(u8, base, @as(isize, v.g.vs_battle_floatingLabelCount().*) * elem_size);
    c.vs_main_bzero(last, @as(c_int, elem_size));
}
