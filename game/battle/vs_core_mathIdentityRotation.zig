//! Sets five fields of a 0x18-byte record to 0x1000 or 0 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: vs_core_mathIdentityRotation (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Initialises the record's scale-like fields (0x1000 = one) and clears the other two.
pub fn vs_core_mathIdentityRotation(arg0: ?*v.func_800CFE98_t) callconv(.c) void {
    const r = arg0.?;
    r.unk10 = 0x1000;
    r.unk8 = 0x1000;
    r.unk0 = 0x1000;
    r.unkC = 0;
    r.unk4 = 0;
}
