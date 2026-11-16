//! An actor's screen-space heading (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const std = @import("std");
const psx = @import("common").psx;
const gte = @import("common").gte;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_actorDialogAnchor (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Projects the point 1000 above actor arg0 through its matrix (func_800A1178), lets vs_actors_targetPoint move
/// it, and stores in out.vz (and returns) the ratan2 angle of the projected screen point against it.
pub fn vs_event_actorDialogAnchor(arg0: c_int, out: *v.SVECTOR) callconv(.c) c_int {
    var matrix = std.mem.zeroes(c.MATRIX); // the C's stack locals, written by the callees
    var sp30 = std.mem.zeroes([5]c_int);
    out.vx = 0;
    out.vy = -0x3E8;
    out.vz = 0;
    _ = c.func_800A1178(arg0, 1, &matrix, @ptrCast(&sp30[2]));
    const view = gte.viewOf(&matrix, gte.screenCurrent());
    const projected = gte.project(&view, out);
    sp30[0] = projected.sxy;
    sp30[3] = projected.ir0;
    sp30[4] = @bitCast(projected.flag);
    _ = c.vs_actors_targetPoint(arg0, 1, psx.cptr(c.SVECTOR, out), sp30[2]);
    const sy: i16 = @truncate(sp30[0] >> 16); // ((short*)&sp30)[1]: the high half of the screen xy
    const sx: i16 = @truncate(sp30[0]); // (short)sp30[0]
    out.vz = psx.conv(i16, c.ratan2(@as(c_int, sy) -% out.vy, @as(c_int, sx) -% out.vx));
    return out.vz;
}
