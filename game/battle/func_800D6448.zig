//! An effect-script opcode: func_800CF7A8 between func_800D78B8 and func_800D78CC (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D6448 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// func_800CF7A8(entry's unk34, unk35, value % 10, value / 10) for entry unk1C[index] (unchecked, as in the C).
pub fn func_800D6448(script: ?*v.D_800F53B8_t, index: u8, value: u8) callconv(.c) void {
    const s = script.?;
    c.func_800D78B8();
    const entry = psx.at(&s.unk1C, index);
    c.func_800CF7A8(entry.unk34, entry.unk35, value % 10, value / 10);
    c.func_800D78CC();
}
