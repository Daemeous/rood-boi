//! The battle's per-frame wait on a target (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80078248 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Counts the frames a target is held and closes the wait after 31 of them, or once the chosen target is found.
pub fn func_80078248() callconv(.c) void {
    const state = v.g.vs_battle_actionState().*.?;
    if (state.unk2984 != 0) {
        if (state.unk8.unk4.unk0.targetActor != 0) {
            state.unk298E +%= 1;
            if (state.unk298E >= 0x1F) {
                c.vs_battle_commitSelectedAction(1);
            }
        } else if (c.vs_battle_updateTargetSelectionMenu(&state.unk298D) != 0) {
            state.unk298C = state.unk298D;
            c.vs_battle_commitSelectedAction(1);
        }
    } else {
        c.vs_battle_commitSelectedAction(1);
    }
}
