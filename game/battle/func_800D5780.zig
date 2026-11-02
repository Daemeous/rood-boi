//! Advances an effect command list (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D5780 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// 0 at the last command; else the next command (unk6), set up by its kind (1: func_800D5700, 13: func_800D5738), and 1.
pub fn func_800D5780(cmd_arg: ?*v.func_800D5780_t) callconv(.c) c_int {
    const cmd = cmd_arg.?;
    if (@as(c_int, cmd.unk6) + 1 >= psx.getBits(&cmd.unk4, "unk0_8")) { // i16 + 1: no overflow in c_int
        return 0;
    }
    cmd.unk6 +%= 1;
    switch (psx.getBits(&cmd.unk4, "unk0_0")) {
        1 => c.func_800D5700(@ptrCast(cmd)),
        0xD => c.func_800D5738(@ptrCast(cmd)),
        else => {},
    }
    return 1;
}
