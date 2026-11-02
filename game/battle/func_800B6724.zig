//! Reads one flag bit from the struct behind the battle global D_800EB9BC (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");

/// orig: func_800B6724 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns bit 1 of the first byte of the struct that D_800EB9BC points to.
pub fn func_800B6724(_: [*c]u8, _: c_short) callconv(.c) c_int {
    const block: *v.D_800EB9BC_t = v.g.D_800EB9BC().*.?;
    return @as(c_int, psx.getBits(block, "unk0_1"));
}
