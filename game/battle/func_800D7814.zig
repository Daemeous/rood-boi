//! Resets the battle file pointers and curve data, then runs the two setup calls
//! (src/BATTLE/BATTLE.PRG/6F014.c).
const psx = @import("common").psx;
const v = @import("views_6F014");
const c = @import("c_6F014");

/// orig: func_800D7814 (src/BATTLE/BATTLE.PRG/6F014.c)
/// Clears the battle file pointers and counters, points the curve data and D_800F56C8's pointer field at their tables, then runs the two setup calls.
pub fn func_800D7814() callconv(.c) void {
    v.g.D_800F568C().* = null;
    v.g.vs_battle_pfileBuf().* = null;
    v.g.D_800F56A0().* = null;
    v.g.D_800F5874().* = 0;
    v.g.D_800F5684().* = 0;
    psx.put(&v.g.D_800F57A0().curveBlock, @as(?*v.pFileCurveBlock, null));
    psx.put(&v.g.D_800F57A0().unkD0, v.g.D_800F53C0());
    // ROOD_PC branch: D_800F5798 is D_800F56C8.unkD0, stored as a guest pointer in the struct.
    psx.put(&v.g.D_800F56C8().unkD0, v.g.D_800F5230());
    c.func_800D7890(0);
    c.func_800D8060(psx.cptr(c.p_file_t, v.g.D_800EC4BC()));
}
