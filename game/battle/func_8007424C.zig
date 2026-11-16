//! Reports whether the battle flag from vs_actors_actorStateFlags is set and func_800CB45C returns 0 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: func_8007424C (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns 1 when vs_actors_actorStateFlags(0) has bit 27 set and func_800CB45C() returns 0, else 0.
pub fn func_8007424C() callconv(.c) c_int {
    var ret: c_int = 0;
    if ((c.vs_actors_actorStateFlags(0) & 0x08000000) != 0) {
        ret = @intFromBool(c.func_800CB45C() == 0);
    }
    return ret;
}
