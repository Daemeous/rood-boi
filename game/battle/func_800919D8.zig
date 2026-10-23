//! Sets flag 0x100 on the record func_8009195C finds for an index (src/BATTLE/BATTLE.PRG/2842C.c).
const c = @import("c_2842C");

/// orig: func_800919D8 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Looks up the record for `arg0` and sets its 0x100 flag; returns 1 if the record exists, else 0.
pub fn func_800919D8(arg0: c_int) callconv(.c) c_int {
    const temp_v0 = c.func_8009195C(arg0);
    if (temp_v0 == null) return 0;
    const flags: *align(1) c_int = @ptrCast(temp_v0.?);
    flags.* |= 0x100;
    return 1;
}
