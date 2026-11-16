//! Stores a parsed p-file block 3 pointer in the vs_effects_package block table (pc/src/game/BATTLE/BATTLE.PRG/6F014.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");

/// orig: _parsePfileBlock3 (pc/src/game/BATTLE/BATTLE.PRG/6F014.c)
/// Records `block` as the guest address of the parsed p-file block 3 data.
pub fn parsePfileBlock3(block: ?*anyopaque) callconv(.c) void {
    // The C dereferences vs_effects_package unconditionally: no null guard, so `.?` traps where it would crash.
    const data = v.g.vs_effects_package().*.?;
    psx.put(&data.block3Data, block);
}
