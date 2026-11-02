//! An actor's screen-space heading (src/BATTLE/BATTLE.PRG/4A0A8.c).
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BB7C4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Projects the point 1000 above actor arg0 through its matrix (func_800A1178), lets func_800A1280 move
/// it, and stores in out.vz (and returns) the ratan2 angle of the projected screen point against it.
pub fn func_800BB7C4(arg0: c_int, out: *v.SVECTOR) callconv(.c) c_int {
    var matrix = std.mem.zeroes(c.MATRIX); // the C's stack locals, written by the callees
    var sp30 = std.mem.zeroes([5]c_int);
    out.vx = 0;
    out.vy = -0x3E8;
    out.vz = 0;
    _ = c.func_800A1178(arg0, 1, &matrix, @ptrCast(&sp30[2]));
    c.SetRotMatrix(&matrix);
    c.SetTransMatrix(&matrix);
    _ = c.RotTransPers(psx.cptr(c.SVECTOR, out), &sp30[0], &sp30[3], &sp30[4]);
    _ = c.func_800A1280(arg0, 1, psx.cptr(c.SVECTOR, out), sp30[2]);
    const sy: i16 = @truncate(sp30[0] >> 16); // ((short*)&sp30)[1]: the high half of the screen xy
    const sx: i16 = @truncate(sp30[0]); // (short)sp30[0]
    out.vz = psx.conv(i16, c.ratan2(@as(c_int, sy) -% out.vy, @as(c_int, sx) -% out.vx));
    return out.vz;
}
