//! Advances an effect command list (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_advanceTimelineKey (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// 0 at the last command; else the next command (unk6), set up by its kind (1: vs_effects_particleKeyTimingOf, 13: vs_effects_shakeKeyTimer), and 1.
pub fn vs_effects_advanceTimelineKey(cmd_arg: ?*v.func_800D5780_t) callconv(.c) c_int {
    const cmd = cmd_arg.?;
    if (@as(c_int, cmd.unk6) + 1 >= psx.getBits(&cmd.unk4, "unk0_8")) { // i16 + 1: no overflow in c_int
        return 0;
    }
    cmd.unk6 +%= 1;
    switch (psx.getBits(&cmd.unk4, "unk0_0")) {
        1 => c.vs_effects_particleKeyTimingOf(@ptrCast(cmd)),
        0xD => c.vs_effects_shakeKeyTimer(@ptrCast(cmd)),
        else => {},
    }
    return 1;
}
