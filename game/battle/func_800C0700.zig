//! Copies a record into the target shape and clears its unk2A
//! (src/BATTLE/BATTLE.PRG/573B8.c).
const psx = @import("common").psx;
const v = @import("views_573B8");

/// orig: func_800C0700 (src/BATTLE/BATTLE.PRG/573B8.c)
/// Copies the record at `src` into the target shape's unk10 and clears its unk2A.
pub fn func_800C0700(arg0: ?*v.D_800EB9B8_t2) callconv(.c) void {
    // ROOD_PC: with the shape freed, the original writes PS1 address 0 (psx.lowRam).
    const shape: *v.D_800EB9B8_t = if (v.g.D_800EB9B8().*) |p| p else @alignCast(psx.lowRam(v.D_800EB9B8_t));
    shape.unk10 = arg0.?.*;
    shape.unk2A = 0;
}
