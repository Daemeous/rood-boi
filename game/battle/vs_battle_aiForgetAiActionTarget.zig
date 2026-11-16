//! Battle overlay unit 7C4E8: a single field setter (pc/src/game/BATTLE/BATTLE.PRG/7C4E8.c).
const v = @import("views_7C4E8");

/// orig: vs_battle_aiForgetAiActionTarget (pc/src/game/BATTLE/BATTLE.PRG/7C4E8.c)
/// Sets the record's unk38 field to -1.
pub fn vs_battle_aiForgetAiActionTarget(arg0: ?*v.vs_battle_aiState) callconv(.c) void {
    arg0.?.previous.previousView = -1;
}
