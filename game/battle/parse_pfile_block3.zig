//! Stores a parsed p-file block 3 pointer in the D_800F569C block table (src/BATTLE/BATTLE.PRG/6F014.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");

/// orig: _parsePfileBlock3 (src/BATTLE/BATTLE.PRG/6F014.c)
/// Records `block` as the guest address of the parsed p-file block 3 data.
pub fn parsePfileBlock3(block: ?*anyopaque) callconv(.c) void {
    // The C dereferences D_800F569C unconditionally: no null guard, so `.?` traps where it would crash.
    const data = v.g.D_800F569C().*.?;
    psx.put(&data.block3Data, block);
}
