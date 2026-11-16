//! Reads one flag bit from the struct behind the battle global vs_render_gimLayers (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");

/// orig: func_800B6724 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns bit 1 of the first byte of the struct that vs_render_gimLayers points to.
pub fn func_800B6724(_: [*c]u8, _: c_short) callconv(.c) c_int {
    const block: *v.D_800EB9BC_t = v.g.vs_render_gimLayers().*.?;
    return @as(c_int, psx.getBits(block, "unk0_1"));
}
