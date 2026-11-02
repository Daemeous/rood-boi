//! An event script opcode that turns an actor to a heading (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B7DC4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Turns the actor of getShort(script + 1) to heading script[3] << 4 (script[5] the third
/// argument, 0xFF as -1): with script[4] 0 at once (func_800A9EB4), else by the turn from its
/// func_800A1108 heading, wrapped to a half circle and, for script[4] -1 or 1, forced
/// clockwise or counterclockwise (func_800A9E38); then its D_800F4B70 mark.
pub fn func_800B7DC4(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var state: v.func_8006EBF8_t = undefined;
    const actor: u16 = psx.conv(u16, c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1))));
    const third: i16 = if (script[5] != 0xFF) script[5] else -1;
    if (script[4] != 0) {
        _ = c.func_800A1108(actor, @ptrCast(&state));
        var turn: c_int = (@as(c_int, script[3]) << 4) - state.unk0.unk4.pad;
        if (turn < -0x1000 / 2) turn += 0x1000;
        if (turn > 0x1000 / 2) turn -= 0x1000;
        switch (@as(i8, @bitCast(script[4]))) {
            -1 => {
                if (turn > 0) turn -= 0x1000;
            },
            1 => {
                if (turn < 0) turn += 0x1000;
            },
            else => {},
        }
        c.func_800A9E38(actor, psx.conv(c_short, turn), third);
    } else {
        c.func_800A9EB4(actor, psx.conv(c_short, @as(c_int, script[3]) << 4), third);
    }
    v.g.D_800F4B70()[actor] = 1;
    return 0;
}
