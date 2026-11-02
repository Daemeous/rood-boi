//! The event script's conditional jump on a state flag and an immediate (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: vs_battle_script_jumpFwdIfFlag (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Compares a state flag with script[3] by script[1]'s test; on a pass returns the script address
/// script + getShort(script + 4), else 0 (no jump). Tests 0x70..0xF0 always jump.
pub fn script_jumpFwdIfFlag(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const flag: c_int = (@as(c_int, script[2]) >> 2) | ((@as(c_int, script[1]) & 0xF) << 6);
    const a: c_int = c.vs_battle_getStateFlag(psx.conv(i16, flag));
    const b: c_int = script[3];
    switch (@as(c_int, script[1]) & 0xF0) {
        0 => if (b >= a) return 0,
        16 => if (a >= b) return 0,
        32 => if (a == b) return 0,
        48 => if (a != b) return 0,
        64 => if (a < b) return 0,
        80 => if (b < a) return 0,
        96 => if ((a & b) != 0) return 0,
        else => {},
    }
    return @bitCast(psx.addr(psx.at(script, c.vs_battle_getShort(script + 4))));
}
