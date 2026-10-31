//! Checks a battle entry's table slot against its second value and reports the result (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D4B34 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Compares the slot `unk10` selects against the second value and passes the result on; always returns 1.
pub fn func_800D4B34(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const self = arg0.?;
    const handle: ?*c.D_800F53B8_t = @ptrCast(self);

    // Object order: the index call, then the slot load, then the second call, then the compare.
    const index = c.func_800D5170(handle);
    const entry: c_int = @as(c_int, psx.at(&self.unk10, index).*);
    const other: c_int = @as(c_int, c.func_800D5198(handle));

    c.func_800D46DC(@intFromBool(entry == other), handle);
    return 1;
}
