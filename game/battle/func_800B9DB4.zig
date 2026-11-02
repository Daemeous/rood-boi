//! An event script opcode that sets or clears actor 0's 0x1800 flags, with an animation (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B9DB4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Actor 0's stance by script[1] (set or clear): when idle (D_800F4C4E 0) and state flag 0xAC is
/// set, func_8007C0AC with D_800E9B4C[script[1]], and outside D_800F4C2C 2 with script[2] set the
/// animation (func_800A0204) and func_8009DBDC, waiting (D_800F4C4E 1, returns 1); while waiting,
/// D_800F4C2C 2 ends it at once, else once func_800A0BE0 bit 0x80 is set it plays the end
/// animation and returns 0. Otherwise actor 0's flags 0x1800 are set or cleared by script[1]
/// (func_800A087C, then func_8009D88C).
pub fn func_800B9DB4(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const waiting = v.g.D_800F4C4E();
    if (waiting.* == 0) {
        if (c.vs_battle_getStateFlag(0xAC) == 0) return 0;
        c.func_8007C0AC(0, v.g.D_800E9B4C()[script[1]]);
        if (v.g.D_800F4C2C().* != 2 and script[2] != 0) {
            _ = c.func_800A0204(0, (@as(c_int, script[2]) - 1) * 2 + ((@as(c_int, script[1]) ^ 1) + 0x6D), 0, 10);
            c.func_8009DBDC(0, @as(c_int, script[1]) ^ 1);
            waiting.* = 1;
            return 1;
        }
    } else if (v.g.D_800F4C2C().* == 2) {
        waiting.* = 0;
    } else {
        if (c.func_800A0BE0(0) & 0x80 == 0) return 1;
        _ = c.func_800A0204(0, if (script[1] != 0) 0x65 else 6, 0, 10);
        waiting.* = 0;
        return 0;
    }
    var flags: c_uint = c.func_800A0BE0(0);
    if (script[1] != 0) {
        flags = (flags | 0x1800) & 0xFFFF;
    } else {
        flags &= 0xE7FF;
    }
    c.func_800A087C(0, psx.conv(c_int, flags));
    c.func_8009D88C(0);
    return 0;
}
