//! Clears the flags in the `unk1848` block of a battle actor, keeping `unk0` set only when `unk0.unkF` is nonzero (src/BATTLE/BATTLE.PRG/3A1A0.c).
const v = @import("views_3A1A0");

/// orig: func_800A525C (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Resets the `unk1848` block's counters and sets its flag from `unk0.unkF`.
pub fn func_800A525C(arg0: ?*v.D_800F4538_t) callconv(.c) void {
    const p = arg0.?;
    p.unk1848.unk0 = 1;
    if (p.unk0.unkF == 0) {
        p.unk1848.unk0 = 0;
    }
    p.unk1848.unk4 = 0;
    p.unk1848.unk8 = 0;
    p.unk1848.unk6 = 0;
}
