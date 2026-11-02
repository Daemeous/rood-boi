//! After an action's timing input: the chain or defense ability it triggers (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// D_800F19CC, read at each use as the C does.
fn state() *v.D_800F19CC_t {
    return v.g.D_800F19CC().*.?;
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

/// orig: func_8006FD1C (src/BATTLE/BATTLE.PRG/146C.c)
/// Tick speed 2; a Break Art or battle-ability action (0x8D..0xB7, 0xB8..0xDF) plays its user's pose 5 or 4. From
/// the 999th chain link, or with no ability input, the action ends (func_8006FD0C). A chain input (1) on a live
/// target queues the mapped chain ability as the next link (raising the max chain, and counting toward the next
/// battle ability from the 10th link); a defense input (2) folds the hits on the player into one, aims the mapped
/// defense ability at the attacker, and counts toward the next battle ability.
pub fn func_8006FD1C() callconv(.c) void {
    var saved: v._hitEntity_t = undefined;
    var position: v.func_8006EBF8_t = undefined;
    v.g.vs_gametime_tickspeed().* = 2;
    var action = &state().unk854[state().unk0 & 3];
    if (action.actionIndex >= 0x8D and action.actionIndex < 0xB8) {
        _ = c.func_8009CC20(action.unk4.unk0.targetActor, 5);
    } else if (action.actionIndex >= 0xB8 and action.actionIndex < 0xE0) {
        _ = c.func_8009CC20(action.unk4.unk0.targetActor, 4);
    }
    if (state().unk0 >= 999) {
        c.func_8006FD0C();
        return;
    }
    // unk2C03 - 1 indexes the 4 mapped abilities: the C does not bound it.
    if (state().battleAbilityInputSuccessful == 1 and stats(action.unk4C[0].unk0.targetActor).currentHP != 0) {
        c.func_800DEEA4(@ptrCast(action));
        v.g._cameraMode().* = 8;
        state().unk4 +%= 1;
        state().unk0 +%= 1;
        c.func_80085B10(psx.at(&v.g.vs_main_settings().mappedChainAbilities, @as(c_int, state().unk2C03) - 1).*,
            @ptrCast(&state().unk854[state().unk0 & 3]), @ptrCast(&state().unk8), 1);
        c.func_80093F24(state().unk0);
        const score = v.g.scoreData();
        if (score.maxChain < state().unk0) {
            score.maxChain = psx.conv(u16, state().unk0);
        }
        if (c.vs_battle_getStateFlag(0x82) >= 10) {
            countKill();
        }
    } else if (state().battleAbilityInputSuccessful == 2) {
        c.func_800DEEA4(@ptrCast(action));
        v.g._cameraMode().* = 8;
        state().unk4 +%= 1;
        saved = state().unk8.unk4;
        var found = false;
        var i: c_int = 0;
        // unk4C has 30 entries; the hit count unk4A is not bounded by the C.
        while (i < state().unk8.unk4A) : (i += 1) { // i < a u16
            const hit = psx.at(&state().unk8.unk4C, i);
            if (hit.unk0.targetActor == 0 and (psx.getBits(hit, "hpEffect") != 0 or psx.getBits(hit, "mpEffect") != 0)) {
                state().unk8.unk4 = hit.*;
                found = true;
            }
        }
        if (!found) {
            state().unk8.unk4 = state().unk8.unk4C[0];
        }
        state().unk8.unk4.unk0.mp = 0;
        state().unk8.unk4.unk0.hp = 0;
        i = 0;
        while (i < state().unk8.unk4A) : (i += 1) { // i < a u16
            const hit = psx.at(&state().unk8.unk4C, i);
            if (hit.unk0.targetActor == state().unk8.unk4.unk0.targetActor) {
                state().unk8.unk4.unk0.hp = psx.conv(i16, @as(c_int, state().unk8.unk4.unk0.hp) +% hit.unk0.hp);
                state().unk8.unk4.unk0.mp = psx.conv(i16, @as(c_int, state().unk8.unk4.unk0.mp) +% hit.unk0.mp);
            }
        }
        state().unk8.unk4C[0] = saved;
        state().unk8.unk4C[0].unk0.targetLimb = psx.conv(i8, psx.getBits(stats(state().unk8.unk4C[0].unk0.targetActor), "unk36"));
        state().unk8.unk4A = 1;
        _ = c.func_800A1108(saved.unk0.targetActor, &position);
        state().unk8.unk844.vx = position.unk0.unk4.vx;
        state().unk8.unk844.vz = position.unk0.unk4.vz;
        state().unk8.unk844.vy = position.unk0.unk4.vy;
        state().unk0 +%= 1;
        action = &state().unk854[state().unk0 & 3];
        state().unk2C07 = 1;
        c.func_80085B10(psx.at(&v.g.vs_main_settings().mappedDefenseAbilities, @as(c_int, state().unk2C03) - 1).*,
            @ptrCast(action), @ptrCast(&state().unk8), 1);
        countKill();
    } else {
        c.func_8006FD0C();
    }
}
