//! Sets the battle sound and the scale value of the 2842C unit's effect (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_8009121C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Clamps the effect's level into D_800F1DC0, plays its sound if one is set, and stores its scaled value into D_800F1DA4.
pub fn func_8009121C() callconv(.c) void {
    v.g.D_800F1DC0().* = psx.conv(i16, c.vs_battle_clamp(
        psx.conv(c_short, @as(c_int, v.g.D_800F1DC4().*) *% 8),
        4,
        @as(c_int, v.g.D_800F1DC2().*),
    ));

    if (@as(c_int, v.g.D_800F1DBC().*) >= 0) {
        if (psx.at(v.g.D_80068EB4(), v.g.D_800F1DC4().*).* != 0) {
            c.vs_main_playSfxDefault(0x200, @as(c_int, v.g.D_800F1DBC().*));
            c.func_80045BFC(
                0x200,
                @as(c_int, v.g.D_800F1DBC().*),
                @as(c_int, psx.at(v.g.D_80068EB4(), v.g.D_800F1DC4().*).*),
                8,
            );
        } else {
            c.func_80045D64(0x200, @as(c_int, v.g.D_800F1DBC().*));
        }
    }

    const scale: i16 = psx.conv(i16, c.vs_battle_clamp(
        psx.conv(c_short, (@as(c_int, v.g.D_800F1DC4().*) *% 4) +% (@as(c_int, v.g.D_800F1DC4().*) *% 2)),
        12,
        90,
    ));
    const s: c_int = scale;
    v.g.D_800F1DA4().* = psx.conv(i16, s +% (s >> 1) +% (s >> 2));
}
