//! Parses the block-2 header of a PFILE: its count and data pointer (src/BATTLE/BATTLE.PRG/6F014.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");

/// orig: _parsePfileBlock2 (src/BATTLE/BATTLE.PRG/6F014.c)
/// Records block 2's entry count and points the unit's block-2 data at the block.
pub fn parsePfileBlock2(block: ?*v.pFileBlock2) callconv(.c) void {
    const state = v.g.D_800F569C().*.?;
    state.block2Count = block.?.unk0;
    psx.put(&state.block2Data, block);
}
