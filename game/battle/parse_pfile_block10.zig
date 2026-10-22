//! Pfile block 10 loader: stores the block 10 data pointer (src/BATTLE/BATTLE.PRG/6F014.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");

/// orig: _parsePfileBlock10 (src/BATTLE/BATTLE.PRG/6F014.c)
/// Stores the given block 10 data pointer as a guest address in the parsed pfile.
pub fn parsePfileBlock10(arg0: ?*anyopaque) callconv(.c) void {
    psx.put(&v.g.D_800F569C().*.?.block10Data, arg0);
}
