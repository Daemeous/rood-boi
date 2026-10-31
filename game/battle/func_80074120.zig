//! Battle step check: whether the stored target vector has been reached (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80074120 (src/BATTLE/BATTLE.PRG/146C.c)
/// Returns 0 when the stored target is reached and func_800A0BE0(0) has bit 27 set, else 1.
pub fn func_80074120() callconv(.c) c_int {
    const src = &v.g.D_800F1910().unk4.unk0.unk4;
    // The C leaves sp10.pad unset; the object's stack holds 0 there, so start it at 0.
    var sp10: v.SVECTOR = .{ .vx = 0, .vy = 0, .vz = 0, .pad = 0 };

    sp10.vx = src.vx;
    sp10.vy = src.vy;
    sp10.vz = src.vz;

    if ((c._stepTowardTarget(psx.cptr(c.SVECTOR, &sp10), 0, 0x28) == 0) and
        ((c.func_800A0BE0(0) & 0x08000000) != 0))
    {
        return 0;
    }
    return 1;
}
