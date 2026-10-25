//! Sets the pad word of the unk17F4 vector in the battle data block, to 20 when unk63C is above 128 (src/BATTLE/BATTLE.PRG/30DB0.c).
const v = @import("views_30DB0");

/// orig: func_8009FD38 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Sets unk17F4's pad to 30, or to 20 when unk63C is above 128.
pub fn func_8009FD38(arg0: ?*v.D_800F4538_t) callconv(.c) void {
    arg0.?.unk17F4.pad = 30;
    if (@as(c_int, arg0.?.unk63C) > 128) {
        arg0.?.unk17F4.pad = 20;
    }
}
