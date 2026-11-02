//! The moving platform an actor stands on, found by its id
//! (src/BATTLE/BATTLE.PRG/3A1A0.c).
const psx = @import("common").psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A4A24 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Lists the room's platforms (func_8008D2C0) into the C's static table and
/// returns the entry whose unk6 is `arg0 - 2`. None: PS1 address 0 (the PC C
/// returns psx.lowRam where the PS1 returned NULL, which callers read).
/// The PS1 returned a pointer into its stack frame (BUG in the original); the
/// PC C keeps the table static (the build globalises it: views
/// func_800A4A24__sp10), so the caller's reads see the listed values.
pub fn func_800A4A24(arg0: c_int) callconv(.c) ?*v.func_8008D2C0_t {
    const table = v.g.func_800A4A24__sp10();
    const id = arg0 -% 2;
    const count = c.func_8008D2C0(@ptrCast(table));
    var i: c_int = 0;
    while (i < count) : (i += 1) {
        const entry = psx.at(table, i);
        if (@as(c_int, entry.unk6) == id) return entry;
    }
    return @alignCast(psx.lowRam(v.func_8008D2C0_t));
}
