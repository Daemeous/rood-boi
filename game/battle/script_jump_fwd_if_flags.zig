//! The event script's conditional jump on two state flags (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: vs_battle_script_jumpFwdIfFlags (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Compares two state flags by script[1]'s test; on a pass returns the script address
/// script + getShort(script + 4), else 0 (no jump). Tests 0x60..0xF0 always jump.
pub fn script_jumpFwdIfFlags(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const flag0: c_int = (@as(c_int, script[2]) >> 2) | ((@as(c_int, script[1]) & 0xF) << 6);
    const a: c_int = c.vs_battle_getStateFlag(psx.conv(i16, flag0));
    const flag1: c_int = @as(c_int, script[3]) | ((@as(c_int, script[2]) & 3) << 8);
    const b: c_int = c.vs_battle_getStateFlag(psx.conv(i16, flag1));
    switch (@as(c_int, script[1]) & 0xF0) {
        0 => if (a <= b) return 0,
        16 => if (a >= b) return 0,
        32 => if (a == b) return 0,
        48 => if (a != b) return 0,
        64 => if (a < b) return 0,
        80 => if (a > b) return 0,
        else => {},
    }
    return @bitCast(psx.addr(psx.at(script, c.vs_battle_getShort(script + 4))));
}
