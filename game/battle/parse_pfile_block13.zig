//! Parse-file block 13 hookup for the battle overlay (pc/src/game/BATTLE/BATTLE.PRG/6F014.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");
const c = @import("c_6F014");

/// orig: _parsePfileBlock13 (pc/src/game/BATTLE/BATTLE.PRG/6F014.c)
/// Stores block 13's guest address in the parse-file state and records the value vs_audio_bindRoomCutsceneStream derives from it.
pub fn parsePfileBlock13(arg0: ?*anyopaque) callconv(.c) void {
    const state = v.g.vs_effects_package().*.?;
    psx.put(&state.block13Data, arg0);
    state.unkCC = c.vs_audio_bindRoomCutsceneStream(arg0);
}
