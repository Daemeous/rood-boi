//! Builds the 3x3 rotation matrix for an angle, with a fixed tilt on rows 1 and 2
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_buildMinimapRotation (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Fills matrix arg0 from the angle arg1: its sine and cosine plus a fixed tilt.
pub fn vs_field_buildMinimapRotation(arg0: ?*v.MATRIX, arg1: c_int) callconv(.c) void {
    const temp_s0: c_int = c.rsin(arg1);
    const temp_v0: c_int = c.rcos(arg1);
    const a2: c_int = -%temp_s0;

    const m = arg0.?;

    m.m[0][0] = psx.conv(i16, temp_v0);
    m.m[0][1] = 0;
    m.m[0][2] = psx.conv(i16, temp_s0);

    m.m[1][0] = psx.conv(i16, @divTrunc(a2 *% -3138, 4096));
    m.m[1][1] = 2633;
    m.m[1][2] = psx.conv(i16, @divTrunc(-%(temp_v0 *% 3138), 4096));

    m.m[2][0] = psx.conv(i16, @divTrunc(a2 *% 2633, 4096));
    m.m[2][1] = 3138;
    m.m[2][2] = psx.conv(i16, @divTrunc(temp_v0 *% 2633, 4096));
}
