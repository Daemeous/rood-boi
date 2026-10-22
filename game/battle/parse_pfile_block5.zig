//! Stores the pFile block 5 pointer in the pFile block table (src/BATTLE/BATTLE.PRG/6F014.c).
const psx = @import("common").psx;
const v = @import("views_6F014");

/// orig: _parsePfileBlock5 (src/BATTLE/BATTLE.PRG/6F014.c)
/// Points the pFile's block 5 data at `block`.
pub fn parsePfileBlock5(block: ?*v.pFileBlock5) callconv(.c) void {
    const pfile = v.g.D_800F569C().*.?;
    psx.put(&pfile.block5Data, block);
}
