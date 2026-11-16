//! Parses the block-2 header of a PFILE: its count and data pointer (pc/src/game/BATTLE/BATTLE.PRG/6F014.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");

/// orig: _parsePfileBlock2 (pc/src/game/BATTLE/BATTLE.PRG/6F014.c)
/// Records block 2's entry count and points the unit's block-2 data at the block.
pub fn parsePfileBlock2(block: ?*v.pFileBlock2) callconv(.c) void {
    const state = v.g.vs_effects_package().*.?;
    state.block2Count = block.?.animationCount;
    psx.put(&state.block2Data, block);
}
