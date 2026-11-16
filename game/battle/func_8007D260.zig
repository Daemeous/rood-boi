//! Clears an actor slot's state byte unless it is 0x20, then runs vs_actors_stopActorMotion on the slot (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8007D260 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Resets the state byte of battle actor slot `slot` unless it is 0x20, then runs vs_actors_stopActorMotion on the slot.
pub fn func_8007D260(slot: c_int) callconv(.c) void {
    const actor = psx.at(v.g.vs_battle_actors(), slot).*;
    if (actor) |a| {
        if (a.unk8 != 0x20) {
            a.unk8 = 0;
        }
    }
    c.vs_actors_stopActorMotion(slot);
}
