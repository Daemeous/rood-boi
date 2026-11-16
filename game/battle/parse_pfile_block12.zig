//! Parse-file block 12 (pc/src/game/BATTLE/BATTLE.PRG/6F014.c): records the block's address in the file header.
const psx = @import("common").psx;
const v = @import("views_6F014");
const c = @import("c_6F014");

/// orig: _parsePfileBlock12 (pc/src/game/BATTLE/BATTLE.PRG/6F014.c)
/// Stores block 12's address in the file header and passes that address on.
pub fn parsePfileBlock12(arg0: ?*anyopaque) callconv(.c) void {
    const header = v.g.vs_effects_package().*.?;
    psx.put(&header.block12Data, arg0);
    _ = c.vs_audio_bindRoomSfxBank(arg0);
}
