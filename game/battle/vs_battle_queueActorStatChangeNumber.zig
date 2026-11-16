//! Battle actor dispatch: forwards to func_80093B68 unless the actor slot is marked 0x80 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_queueActorStatChangeNumber (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls func_80093B68 for actor slot arg0 unless its unk27 is 0x80, except always for slot 0.
pub fn vs_battle_queueActorStatChangeNumber(arg0: c_int, arg1: c_int, arg2: c_int, arg3: c_int) callconv(.c) void {
    const actor = psx.at(v.g.vs_battle_actors(), arg0).*.?;
    if (@as(c_int, actor.unk27) != 0x80 or arg0 == 0) {
        c.func_80093B68(arg0, arg1, arg2, arg3);
    }
}
