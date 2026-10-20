//! An actor's weapon state (src/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: func_8007C088 (src/BATTLE/BATTLE.PRG/146C.c)
/// 1 when actor `actorId` has its weapon drawn.
pub fn isWeaponDrawn(actorId: c_int) callconv(.c) c_int {
    // vs_battle_actors[actorId]: the C does not check the id.
    return psx.at(v.g.vs_battle_actors(), actorId).*.?.weaponDrawn & 1;
}
