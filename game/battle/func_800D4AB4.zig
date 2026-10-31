//! Battle-unit helper that copies a table entry into a record's slot 3 when the selector is below 4 (src/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: func_800D4AB4 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Copies the table entry selected by the global index into `arg0.unk10[3]` when the index is below 4; always returns 1.
pub fn func_800D4AB4(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const index: u8 = v.g.D_800F522C().*;
    if (@as(c_int, index) < 4) {
        arg0.?.unk10[3] = v.g.D_800EC32C()[@as(usize, index)];
    }
    return 1;
}
