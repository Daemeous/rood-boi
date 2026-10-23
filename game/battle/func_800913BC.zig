//! Stores a new value in the battle's pending sound slot and plays its sound
//! (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_800913BC (src/BATTLE/BATTLE.PRG/2842C.c)
/// Stores `value` as the pending sound and plays it, returning the previous pending value.
pub fn func_800913BC(value: c_int) callconv(.c) c_int {
    const previous: c_int = v.g.D_800F1DBC().*;
    v.g.D_800F1DBC().* = psx.conv(i16, value);

    if (value >= 0) {
        if (psx.at(v.g.D_80068EB4(), v.g.D_800F1DC4().*).* != 0) {
            c.vs_main_playSfxDefault(0x200, @as(c_int, psx.conv(i16, value)));
            c.func_80045BFC(
                0x200,
                @as(c_int, v.g.D_800F1DBC().*),
                @as(c_int, psx.at(v.g.D_80068EB4(), v.g.D_800F1DC4().*).*),
                @as(u8, 8),
            );
        } else {
            c.func_80045D64(0x200, @as(c_int, psx.conv(i16, value)));
        }
    }
    return previous;
}
