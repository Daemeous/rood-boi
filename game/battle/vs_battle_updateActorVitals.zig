//! The per-tick upkeep of every actor (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

fn every(period: c_int) bool {
    return @rem(psx.Global(c_int, "vs_battle_vitalsTickCounter").ptr().*, period) == 0;
}

/// A defeated actor's reaction (vs_battle_resolveActorDefeat); for an enemy that does not react, the end of the action is
/// asked (vs_event_evaluateRoomEventTriggers(0xC)): 1 when it ends, else 2.
fn defeated(i: c_int) c_int {
    var result = c.vs_battle_resolveActorDefeat(0, i, 0);
    if (result == 0 and i != 0) {
        if (c.vs_event_evaluateRoomEventTriggers(0xC, 0, 0, 0) == 1) {
            result = 1;
        } else {
            result = 2;
        }
    }
    return result;
}

fn lowerRisk(stats: *v.vs_battle_actor2) void {
    if (stats.risk != 0) {
        stats.risk -= 1; // not 0
    }
}

fn regenerateMp(actor: *v.vs_battle_actor, stats: *v.vs_battle_actor2) void {
    if (stats.currentMP < stats.maxMP) {
        stats.currentMP +%= 1;
    }
    if (actor.weaponDrawn & 1 != 0) {
        if (stats.weapon.currentPp != 0) {
            stats.weapon.currentPp -= 1; // not 0
        }
        if (stats.shield.currentPp != 0) {
            stats.shield.currentPp -= 1; // not 0
        }
    }
}

fn regenerateHp(stats: *v.vs_battle_actor2) void {
    if (stats.currentHP < stats.maxHP) {
        if (stats.currentHP != 0) {
            stats.currentHP +%= 1;
        }
    }
    // the limb vs_battle_regeneratingLimb cycles 0..5; limbs has 6 entries (the C does not bound it)
    const limb = psx.at(&stats.limbs, psx.Global(c_int, "vs_battle_regeneratingLimb").ptr().*);
    if (limb.maxHp != 0) {
        if (limb.hp < limb.maxHp) {
            limb.hp +%= 1;
        }
    }
}

/// orig: vs_battle_updateActorVitals (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// For each actor (the player, and the others not unk27 0x80): a dying one (0x1000000) not yet down reacts; an
/// idle one cools its unk954 by 35 and, every 30 ticks while alive, lowers its risk and regenerates MP (wearing
/// PP with its weapon drawn) and HP and one limb's HP, at a slower pace with its weapon drawn; every 60 ticks it
/// runs its status upkeep (vs_battle_updateActorStatusTimers) and, at 0 HP, falls (vs_battle_beginActorDefeatAnimation) or reacts. Then the tick counter
/// vs_battle_vitalsTickCounter advances (and the regenerating limb every 240), the event timer runs (vs_state_advanceSceneTimer). Returns 1 when
/// the action ends, 2 when an enemy fell, else 0 (the timer's result first).
pub fn vs_battle_updateActorVitals() callconv(.c) c_int {
    var result: c_int = 0;
    var i: c_int = 0;
    while (i < 16) : (i += 1) { // i < 16
        const actor = v.g.vs_battle_actors()[@intCast(i)];
        var outcome: c_int = 0;
        if (actor != null and (actor.?.unk27 != 0x80 or i == 0)) {
            const status: c_int = @bitCast(c.vs_actors_actorStateFlags(i));
            if (status & 0x01000000 != 0) {
                if (c.vs_actors_isActorActionAnimationBusy(i) != 1) {
                    outcome = defeated(i);
                }
            } else if ((status & 0x01002001) == 1) {
                if (psx.get(actor.?.stats)) |stats| {
                    if (stats.unk954 > 35) {
                        stats.unk954 -= 35;
                    } else if (stats.unk954 != 0) {
                        stats.unk954 = 0;
                    }
                    if (every(30) and stats.currentHP != 0) {
                        if (actor.?.weaponDrawn & 1 != 0) {
                            if (every(120)) {
                                lowerRisk(stats);
                            }
                            if (every(180)) {
                                regenerateMp(actor.?, stats);
                            }
                            if (every(240)) {
                                regenerateHp(stats);
                            }
                        } else {
                            if ((stats.statuses & 8) == 0 or every(120)) {
                                lowerRisk(stats);
                            }
                            if (every(60)) {
                                regenerateMp(actor.?, stats);
                            }
                            if (every(120)) {
                                regenerateHp(stats);
                            }
                        }
                    }
                    if (every(60)) {
                        if (stats.currentHP != 0) {
                            c.vs_battle_updateActorStatusTimers(@ptrCast(stats));
                        }
                        if (stats.currentHP == 0) {
                            if (actor.?.unk26 == 0) {
                                _ = c.vs_battle_beginActorDefeatAnimation(i);
                            } else {
                                outcome = defeated(i);
                            }
                        }
                    }
                }
            }
        }
        if (outcome != 0) {
            if (result == 0) {
                result = outcome;
            } else if (outcome == 1) {
                result = 1;
            }
        }
    }
    const ticks = psx.Global(c_int, "vs_battle_vitalsTickCounter").ptr();
    ticks.* = @rem(ticks.* +% v.g.vs_gametime_tickspeed().*, 720);
    if (every(240)) {
        const limb = psx.Global(c_int, "vs_battle_regeneratingLimb").ptr();
        limb.* = @rem(limb.* +% 1, 6);
    }
    const timer = c.vs_state_advanceSceneTimer();
    if (timer != 0) {
        result = timer;
    }
    return result;
}
