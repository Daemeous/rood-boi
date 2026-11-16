//! Clears the battle ability target window when the targeted entity is set (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_armDefenseAbilityTimingWindow (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Opens the battle ability target window for the current hit entity, unless arg0 is set or no target is recorded.
pub fn vs_battle_armDefenseAbilityTimingWindow(arg0: c_int) callconv(.c) void {
    if (arg0 != 0) {
        return;
    }

    const d = v.g.vs_battle_actionState().*.?;
    const idx = d.unk0 & 3;

    if (d.queue[idx].primaryTarget.target.targetActor == 0) {
        return;
    }

    // The switch on (targetActor != 0) is 1 here, and its only case is 0: nothing runs.

    if (d.battleAbilityTargetWindow == 0 and c.vs_battle_canTriggerBattleAbility(0) != 0) {
        // Re-read the global after the call, as the C does.
        const e = v.g.vs_battle_actionState().*.?;
        e.battleAbilityTargetWindow = psx.conv(u16, @as(c_int, e.battleAbilityTimer) +%
            @as(c_int, v.g.vs_gametime_tickspeed().*) *% 2);
    }
}
