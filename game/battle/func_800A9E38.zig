//! Calls func_800AA984 for one battle entry with its unk5C0 field set to a value for the duration of the call (src/BATTLE/BATTLE.PRG/40564.c).
const psx = @import("common").psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800A9E38 (src/BATTLE/BATTLE.PRG/40564.c)
/// Calls func_800AA984 for entry `arg0` with its unk5C0 set to `arg2` for the call, then restores it.
pub fn func_800A9E38(arg0: c_int, arg1: c_short, arg2: c_int) callconv(.c) void {
    // The entry is read before the test; it is only dereferenced on the path that uses it.
    const entry: ?*v.D_800F4538_t = psx.at(v.g.D_800F4538(), arg0).*;

    if (arg2 == -1) {
        c.func_800AA984(arg0, arg1, -1);
        return;
    }

    const e = entry.?;
    const saved: u16 = e.unk5C0;
    e.unk5C0 = psx.conv(u16, arg2);

    c.func_800AA984(arg0, arg1, -1);

    e.unk5C0 = saved;
}
