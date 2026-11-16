//! The battle's per-frame wait check before its action step runs (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_800782E4 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Advances the wait counter while a target is set, else asks vs_battle_updateSphereTargetPosition; runs vs_battle_filterAreaActionTargets once the wait is over.
pub fn func_800782E4() callconv(.c) void {
    const state = v.g.vs_battle_actionState().*.?;
    if (state.unk2984 != 0) {
        if (state.unk8.primaryTarget.target.targetActor != 0) {
            state.unk298E +%= 1;
            const count: c_uint = state.unk298E;
            if (count < 0x1F) return;
        } else if (c.vs_battle_updateSphereTargetPosition(psx.cptr(c.SVECTOR, &state.unk2998)) == 0) {
            return;
        }
    }
    c.vs_battle_filterAreaActionTargets();
}
