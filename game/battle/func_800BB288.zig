//! The battle's pending-state test (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: func_800BB288 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns 1 when the global D_800EB9B4 points at a record whose first byte is nonzero, else 0.
pub fn func_800BB288(_: [*c]u8, _: c_short) callconv(.c) c_int {
    if (v.g.D_800EB9B4().*) |node| {
        if (node.unk0 != 0) return 1;
    }
    return 0;
}
