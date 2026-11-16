//! After an action's timing input: the chain or defense ability it triggers (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// vs_battle_actionState, read at each use as the C does.
fn state() *v.D_800F19CC_t {
    return v.g.vs_battle_actionState().*.?;
}

fn stats(actor: u8) *v.vs_battle_actor2 {
    return psx.get(v.g.vs_battle_actors()[actor].?.stats).?;
}

/// A battle ability used counts toward the next one, until all 22 are unlocked.
fn countKill() void {
    const kills = &v.g.vs_main_artsStatus().kills;
    if (kills.battleAbilitiesUnlocked < 22 and
        kills.total < v.g.vs_main_battleAbilitiesPointsRequirements()[kills.battleAbilitiesUnlocked])
    {
        kills.total +%= 1;
    }
}

/// orig: vs_battle_advanceBattleAbilitySequence (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Tick speed 2; a Break Art or battle-ability action (0x8D..0xB7, 0xB8..0xDF) plays its user's pose 5 or 4. From
/// the 999th chain link, or with no ability input, the action ends (vs_battle_finishBattleAbilitySequence). A chain input (1) on a live
/// target queues the mapped chain ability as the next link (raising the max chain, and counting toward the next
/// battle ability from the 10th link); a defense input (2) folds the hits on the player into one, aims the mapped
/// defense ability at the attacker, and counts toward the next battle ability.
pub fn vs_battle_advanceBattleAbilitySequence() callconv(.c) void {
    var saved: v._hitEntity_t = undefined;
    var position: v.func_8006EBF8_t = undefined;
    v.g.vs_gametime_tickspeed().* = 2;
    var action = &state().queue[state().unk0 & 3];
    if (action.actionIndex >= 0x8D and action.actionIndex < 0xB8) {
        _ = c.vs_content_releaseActorSequence(action.primaryTarget.target.targetActor, 5);
    } else if (action.actionIndex >= 0xB8 and action.actionIndex < 0xE0) {
        _ = c.vs_content_releaseActorSequence(action.primaryTarget.target.targetActor, 4);
    }
    if (state().unk0 >= 999) {
        c.vs_battle_finishBattleAbilitySequence();
        return;
    }
    // unk2C03 - 1 indexes the 4 mapped abilities: the C does not bound it.
    if (state().battleAbilityInputSuccessful == 1 and stats(action.targets[0].target.targetActor).currentHP != 0) {
        c.vs_battle_aiNotifyActionRelationshipChanges(@ptrCast(action));
        v.g._cameraMode().* = 8;
        state().unk4 +%= 1;
        state().unk0 +%= 1;
        c.vs_battle_resolveActionTargets(psx.at(&v.g.vs_main_settings().mappedChainAbilities, @as(c_int, state().chainAbilitySlot) - 1).*,
            @ptrCast(&state().queue[state().unk0 & 3]), @ptrCast(&state().unk8), 1);
        c.func_80093F24(state().unk0);
        const score = v.g.scoreData();
        if (score.maxChain < state().unk0) {
            score.maxChain = psx.conv(u16, state().unk0);
        }
        if (c.vs_battle_getStateFlag(0x82) >= 10) {
            countKill();
        }
    } else if (state().battleAbilityInputSuccessful == 2) {
        c.vs_battle_aiNotifyActionRelationshipChanges(@ptrCast(action));
        v.g._cameraMode().* = 8;
        state().unk4 +%= 1;
        saved = state().unk8.primaryTarget;
        var found = false;
        var i: c_int = 0;
        // unk4C has 30 entries; the hit count unk4A is not bounded by the C.
        while (i < state().unk8.targetCount) : (i += 1) { // i < a u16
            const hit = psx.at(&state().unk8.targets, i);
            if (hit.target.targetActor == 0 and (psx.getBits(hit, "hpEffect") != 0 or psx.getBits(hit, "mpEffect") != 0)) {
                state().unk8.primaryTarget = hit.*;
                found = true;
            }
        }
        if (!found) {
            state().unk8.primaryTarget = state().unk8.targets[0];
        }
        state().unk8.primaryTarget.target.mp = 0;
        state().unk8.primaryTarget.target.hp = 0;
        i = 0;
        while (i < state().unk8.targetCount) : (i += 1) { // i < a u16
            const hit = psx.at(&state().unk8.targets, i);
            if (hit.target.targetActor == state().unk8.primaryTarget.target.targetActor) {
                state().unk8.primaryTarget.target.hp = psx.conv(i16, @as(c_int, state().unk8.primaryTarget.target.hp) +% hit.target.hp);
                state().unk8.primaryTarget.target.mp = psx.conv(i16, @as(c_int, state().unk8.primaryTarget.target.mp) +% hit.target.mp);
            }
        }
        state().unk8.targets[0] = saved;
        state().unk8.targets[0].target.targetLimb = psx.conv(i8, psx.getBits(stats(state().unk8.targets[0].target.targetActor), "unk36"));
        state().unk8.targetCount = 1;
        _ = c.vs_actors_actorPlacement(saved.target.targetActor, &position);
        state().unk8.unk844.vx = position.location.position.vx;
        state().unk8.unk844.vz = position.location.position.vz;
        state().unk8.unk844.vy = position.location.position.vy;
        state().unk0 +%= 1;
        action = &state().queue[state().unk0 & 3];
        state().unk2C07 = 1;
        c.vs_battle_resolveActionTargets(psx.at(&v.g.vs_main_settings().mappedDefenseAbilities, @as(c_int, state().chainAbilitySlot) - 1).*,
            @ptrCast(action), @ptrCast(&state().unk8), 1);
        countKill();
    } else {
        c.vs_battle_finishBattleAbilitySequence();
    }
}
