//! Resolves an actor reference into a three-byte position written to state flags (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: func_800B7490 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Stores the position of the actor named in the script operand at `flag` and the two bytes after it, or 0xFF there when the actor is absent.
pub fn func_800B7490(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    var position: [12]u8 = undefined;
    var actor: u16 = psx.conv(u16, c.vs_battle_getShort(arg0 + 1));
    const flag: u16 = psx.conv(u16, c.vs_battle_getShort(arg0 + 3));
    const flag0: c_short = psx.conv(c_short, @as(c_int, flag));
    const flag1: c_short = psx.conv(c_short, @as(c_int, flag) +% 1);
    const flag2: c_short = psx.conv(c_short, @as(c_int, flag) +% 2);

    if ((actor & 0x1000) != 0) {
        actor &= 0x1F;
        if (c.func_8007CF64(@as(c_int, actor)) == null) {
            c.vs_battle_setStateFlag(flag0, 0xFF);
            return 0;
        }
    } else {
        actor = psx.conv(u16, c.func_8007CF18(@as(c_int, actor)));
        if (actor == 1) {
            c.vs_battle_setStateFlag(flag0, 0xFF);
            return 0;
        }
    }
    _ = c.func_800A1108(@as(c_int, actor), &position);
    c.vs_battle_setStateFlag(flag0, position[0]);
    c.vs_battle_setStateFlag(flag1, position[1]);
    c.vs_battle_setStateFlag(flag2, position[2]);
    return 0;
}
