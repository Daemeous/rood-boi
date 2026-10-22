//! Sets the pfile's block 11 data pointer (src/BATTLE/BATTLE.PRG/6F014.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");

/// orig: _parsePfileBlock11 (src/BATTLE/BATTLE.PRG/6F014.c)
/// Stores `data` as the block 11 data of the current pfile.
pub fn parsePfileBlock11(data: [*c]u8) callconv(.c) void {
    psx.put(&v.g.D_800F569C().*.?.block11Data, data);
}
