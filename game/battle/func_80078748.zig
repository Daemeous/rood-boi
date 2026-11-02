//! The action's phase 5: waiting for the cast to be ready (src/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");
const v = @import("views_146C");

/// orig: func_80078748 (src/BATTLE/BATTLE.PRG/146C.c)
/// Enters the flash phase (func_800704B0) once func_800CB45C is clear, the target has flags 0x08000100, no object
/// data slot is busy and func_800CEEBC reports ready; each test only after the one before passed.
pub fn func_80078748() callconv(.c) void {
    const state = v.g.D_800F19CC().*.?;
    const slot = &state.unk854[state.unk0 & 3];
    if (c.func_800CB45C() == 0 and
        (c.func_800A0BE0(@as(c_int, slot.unk4.unk0.targetActor)) & 0x08000100) == 0x08000100 and
        c.vs_battle_getEmptyObjectDataSlot() == 0 and c.func_800CEEBC() != 0)
    {
        c.func_800704B0();
    }
}
