//! Battle actor lookup: the byte at offset 0x26 of an actor slot (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_actors_isActorRetainedAfterDefeat (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns byte 0x26 of battle actor slot `arg0`, or 0 when the slot is empty.
pub fn vs_actors_isActorRetainedAfterDefeat(arg0: c_int) callconv(.c) u8 {
    const actor = psx.at(v.g.vs_battle_actors(), arg0).*;
    if (actor) |a| return a.unk26;
    return 0;
}
