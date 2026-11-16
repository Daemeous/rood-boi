//! Stores the pFile block 5 pointer in the pFile block table (pc/src/game/BATTLE/BATTLE.PRG/6F014.c).
const psx = @import("common").psx;
const v = @import("views_6F014");

/// orig: _parsePfileBlock5 (pc/src/game/BATTLE/BATTLE.PRG/6F014.c)
/// Points the pFile's block 5 data at `block`.
pub fn parsePfileBlock5(block: ?*v.pFileBlock5) callconv(.c) void {
    const pfile = v.g.vs_effects_package().*.?;
    psx.put(&pfile.block5Data, block);
}
