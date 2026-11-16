//! The action's phase 5: waiting for the cast to be ready (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");
const v = @import("views_146C");

/// orig: func_80078748 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Enters the flash phase (func_800704B0) once func_800CB45C is clear, the target has flags 0x08000100, no object
/// data slot is busy and func_800CEEBC reports ready; each test only after the one before passed.
pub fn func_80078748() callconv(.c) void {
    const state = v.g.vs_battle_actionState().*.?;
    const slot = &state.queue[state.unk0 & 3];
    if (c.func_800CB45C() == 0 and
        (c.vs_actors_actorStateFlags(@as(c_int, slot.primaryTarget.target.targetActor)) & 0x08000100) == 0x08000100 and
        c.vs_battle_getEmptyObjectDataSlot() == 0 and c.func_800CEEBC() != 0)
    {
        c.func_800704B0();
    }
}
