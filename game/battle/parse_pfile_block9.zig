//! Stores the block 9 pointer of the pFile in the battle globals (src/BATTLE/BATTLE.PRG/6F014.c).
const psx = @import("common").psx;
const v = @import("views_6F014");

/// orig: _parsePfileBlock9 (src/BATTLE/BATTLE.PRG/6F014.c)
/// Sets the block 9 data pointer of the current pFile to `block`.
pub fn parsePfileBlock9(block: ?*v.pFileBlock9) callconv(.c) void {
    psx.put(&v.g.D_800F569C().*.?.block9Data, block);
}
