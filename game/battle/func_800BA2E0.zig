//! Battle script sound request: plays a sound effect from the sound table
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BA2E0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Plays the sound a script names, after recording the volume-like byte when the request is type 2.
pub fn func_800BA2E0(script: [*c]u8) callconv(.c) c_int {
    if (script[1] == 2 and v.g.D_800F4C08().* == 0) {
        return 0;
    }
    if (script[1] == 2) {
        v.g.D_800F4C4C().* = psx.conv(i16, script[2]);
    }
    c.vs_main_playSfx(
        v.g.D_800E9B34()[script[1]],
        @as(c_long, script[2]),
        @as(c_int, script[3]),
        @as(c_int, script[4]),
    );
    return 0;
}
