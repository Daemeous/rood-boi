//! Calls func_800AD714 on battle slot `arg0` and keeps the slot's unk542 and unk544 across the call (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009F8DC (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Calls func_800AD714 on slot `arg0` and restores its unk542 and unk544 afterwards.
pub fn func_8009F8DC(arg0: c_int) callconv(.c) c_int {
    const slot = psx.at(v.g.D_800F4538(), arg0).*.?;
    const s0: *v.D_800F4538_unkC54 = &slot.unkC54;

    const temp_s1 = s0.unk542;
    const temp_s2 = psx.get(s0.unk544);
    const temp_v0 = c.func_800AD714(@ptrCast(slot), @ptrCast(s0), 3);
    s0.unk542 = temp_s1;
    psx.put(&s0.unk544, temp_s2);
    return temp_v0;
}
