//! Stores the block 8 data pointer in the pfile (pc/src/game/BATTLE/BATTLE.PRG/6F014.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");

/// orig: _parsePfileBlock8 (pc/src/game/BATTLE/BATTLE.PRG/6F014.c)
/// Sets the pfile's block 8 data pointer to `block`.
pub fn parsePfileBlock8(block: ?*anyopaque) callconv(.c) void {
    psx.put(&v.g.vs_effects_package().*.?.block8Data, block);
}
