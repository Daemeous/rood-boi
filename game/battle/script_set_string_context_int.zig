//! The event script's text-integer setter (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_battle_script_setStringContextInt (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Loads string-context integer script[1] from one state flag (unsigned or signed byte) or two
/// (a little-endian unsigned or signed half), as script[3]'s top two bits say.
pub fn script_setStringContextInt(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const flag: c_int = @as(c_int, c.vs_battle_getShort(script + 2)) & 0x3FF;
    const id = psx.conv(i16, flag);
    // script[1] is not bounded: past integers[9] the C writes into strings[] and beyond.
    const slot = psx.at(&v.g.stringContext().integers, script[1]);
    switch (script[3] >> 6) {
        0 => slot.* = c.vs_battle_getStateFlag(id),
        1 => slot.* = psx.conv(i8, c.vs_battle_getStateFlag(id)),
        2 => {
            const lo: c_int = c.vs_battle_getStateFlag(id);
            slot.* = lo | (@as(c_int, c.vs_battle_getStateFlag(psx.conv(i16, flag + 1))) << 8);
        },
        3 => {
            const lo: c_int = c.vs_battle_getStateFlag(id);
            slot.* = psx.conv(i16, lo | (@as(c_int, c.vs_battle_getStateFlag(psx.conv(i16, flag + 1))) << 8));
        },
        else => unreachable, // a u8 >> 6
    }
    return 0;
}
