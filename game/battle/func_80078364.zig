//! The battle action wait and finish state machine (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80078364 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Counts down the action wait, then runs the finish of the action (or the battle end) once the wait ends.
pub fn func_80078364() callconv(.c) void {
    if (v.g.vs_battle_actionState().*.?.unk2984 != 0) {
        if (v.g.vs_battle_actionState().*.?.unk8.primaryTarget.target.targetActor != 0) {
            v.g.vs_battle_actionState().*.?.unk298E +%= 1;
            if (@as(c_int, v.g.vs_battle_actionState().*.?.unk298E) >= 0x1F) {
                v.g.vs_battle_actionState().*.?.unk298C = 0;
                c.vs_battle_commitSelectedAction(2);
            }
        } else if (c.vs_battle_updateTargetSelectionMenu(&v.g.vs_battle_actionState().*.?.unk298D) != 0) {
            v.g.vs_battle_actionState().*.?.unk298C = v.g.vs_battle_actionState().*.?.unk298D;
            if (@as(c_int, v.g.vs_battle_actionState().*.?.unk298C) == 0xFF) {
                const action = psx.at(v.g.actions(), v.g.vs_battle_actionState().*.?.unk8.actionIndex);
                if (v.g.D_800E8184()[psx.getBits(action, "unk2_4")] != 0) {
                    c.vs_battle_initializeActionTargeting(@as(c_int, v.g.vs_battle_actionState().*.?.unk8.actionIndex));
                    return;
                }
                c.func_800C16DC();
                c.func_800C06E0();
                c.func_800C0738();
                c.func_800C05B4();
                c.vs_battle_returnToBattleActionMenu();
                return;
            }
            c.vs_battle_commitSelectedAction(2);
        }
    } else {
        v.g.vs_battle_actionState().*.?.unk298C = 0;
        c.vs_battle_commitSelectedAction(2);
    }
}
