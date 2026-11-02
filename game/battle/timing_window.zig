//! The action's phase 7: the chain and defense timing window (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// D_800F19CC, read at each use as the C does.
fn state() *v.D_800F19CC_t {
    return v.g.D_800F19CC().*.?;
}

/// The chain (triangle, square, circle: 0x10, 0x20, 0x80) buttons.
const ability_buttons: u32 = (1 << 4) | (1 << 5) | (1 << 7);

/// An input attempt that missed: no ability.
fn missed() void {
    state().battleAbilityInputSuccessful = 0;
    state().battleAbilityInputAttempted = 1;
}

/// orig: func_80078AB4 (src/BATTLE/BATTLE.PRG/146C.c)
/// One tick of the timing window: its margin (8 with state flag 1, else 10; 4 more for an enemy's action from 54;
/// less by a third of the link, at least 2), the cue when the timer meets the window, the timer advanced. A pressed
/// ability button that is mapped (the player's chain abilities, or the defense ones for an enemy's action) is
/// judged by func_80078828 and graded against the window (early, late, in time, too late): in time and not yet
/// attempted, a new chain button queues the chain ability (the player), or a defense ability for an unaimed first
/// hit (an enemy's action, which counts the streak). Then, for an aimed action, the actor told unless it is busy,
/// and the next step (func_8006FD1C) once the cast flash is over.
pub fn timingWindow() callconv(.c) void {
    const slot = &state().unk854[state().unk0 & 3];
    var margin: c_int = undefined;
    if (slot.unk4.unk0.targetActor == 0) {
        margin = if (c.vs_battle_getStateFlag(1) != 0) 8 else 10;
    } else {
        margin = if (c.vs_battle_getStateFlag(1) != 0) 8 else 10;
        if (slot.actionIndex >= 54) {
            margin += 4;
        }
    }
    // int -= u_int: computed unsigned, the bits kept
    margin = @bitCast(@as(c_uint, @bitCast(margin)) -% state().unk0 / 3);
    if (margin < 2) {
        margin = 2;
    }

    const timer: c_int = state().battleAbilityTimer;
    if (state().battleAbilityTargetWindow != 0) {
        if (timer == @as(c_int, state().battleAbilityTargetWindow) -% v.g.vs_gametime_tickspeed().* *% 2) {
            if (c.func_80078828(0) != 0) {
                c.func_80096FF0(@divTrunc(margin *% 2, v.g.vs_gametime_tickspeed().*));
            }
        }
    }
    state().battleAbilityTimer = psx.conv(u16, @as(c_int, state().battleAbilityTimer) +% v.g.vs_gametime_tickspeed().*);

    const pressed = &v.g.vs_main_buttonsPressed().all;
    if (pressed.* & ability_buttons != 0) {
        var button: c_int = undefined;
        if (pressed.* & (1 << 7) != 0) {
            button = 3;
        } else if (pressed.* & (1 << 4) != 0) {
            button = 2;
        } else if (pressed.* & (1 << 5) != 0) {
            button = 1;
        }
        const index: usize = @intCast(button - 1); // button is 1..3: one of its bits is set
        const settings = v.g.vs_main_settings();

        var mapped = false;
        if (slot.unk4.unk40 == 0) {
            if (slot.unk4.unk0.targetActor == 0) {
                mapped = settings.mappedChainAbilities[index] != 0;
            } else if (settings.mappedDefenseAbilities[index] != 0) {
                mapped = true;
            }
        }

        if (mapped) {
            if (c.func_80078828(button) != 0) {
                if (state().battleAbilityTargetWindow == 0) {
                    _ = c.vs_battle_renderBattleAbilityTimingResult(1);
                    state().battleAbilityInputAttempted = 1;
                } else if (slot.unk4.unk40 == 0) {
                    const window: c_int = state().battleAbilityTargetWindow;
                    if (window -% margin >= timer) {
                        _ = c.vs_battle_renderBattleAbilityTimingResult(0);
                        missed();
                    } else if (@as(c_int, state().battleAbilityTargetWindow) >= timer) {
                        _ = c.vs_battle_renderBattleAbilityTimingResult(1);
                        missed();
                    } else if (@as(c_int, state().battleAbilityTargetWindow) +% margin >= timer) {
                        _ = c.vs_battle_renderBattleAbilityTimingResult(2);
                        if (state().battleAbilityInputAttempted == 0) {
                            const fresh: c_int = ~@as(c_int, state().unk2C02) & @as(c_int, ability_buttons);
                            if (v.g.vs_main_buttonsPressed().all & @as(c_uint, @bitCast(fresh)) != 0) {
                                if (slot.unk4.unk0.targetActor == 0) {
                                    if (state().previousChainInput != button and settings.mappedChainAbilities[index] != 0) {
                                        state().previousChainInput = psx.conv(u8, button);
                                        state().unk2C03 = state().previousChainInput;
                                        state().battleAbilityInputSuccessful = 1;
                                        state().battleAbilityInputAttempted = 1;
                                    } else {
                                        missed();
                                    }
                                } else {
                                    // The loop's index is unused: every pass tests the first hit, as in the C.
                                    var i: c_int = 0;
                                    while (i < slot.unk4A) : (i += 1) {
                                        if (slot.unk4C[0].unk0.targetActor == 0 and slot.unk4C[0].unk40 == 0) {
                                            if (state().previousChainInput != button and settings.mappedDefenseAbilities[index] != 0) {
                                                state().previousChainInput = psx.conv(u8, button);
                                                state().unk2C03 = state().previousChainInput;
                                                state().battleAbilityInputSuccessful = 2;
                                                state().battleAbilityInputAttempted = 1;
                                                const score = v.g.scoreData();
                                                if (score.chainStreak <= 0xFFFE) {
                                                    score.chainStreak += 1; // <= 0xFFFE: no wrap
                                                }
                                            } else {
                                                missed();
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        var grade: c_int = 4;
                        if (@as(c_int, state().battleAbilityTargetWindow) +% margin *% 2 >= timer) {
                            grade = 3;
                        }
                        _ = c.vs_battle_renderBattleAbilityTimingResult(grade);
                        missed();
                    }
                    if (state().battleAbilityInputSuccessful == 0) {
                        psx.global(i16, "D_8005FFB0").* = 0;
                    }
                } else {
                    if (v.g.D_800F1868().* & 3 == 0) {
                        c.func_8006FD1C();
                    }
                    return;
                }
            } else if (state().battleAbilityInputAttempted == 0) {
                missed();
            }
        }
    }

    if (slot.unk4.unk40 == 0) {
        if (c.func_8009E4B0(@as(c_int, slot.unk4.unk0.targetActor)) != 0) {
            return;
        }
        if (state().battleAbilityInputAttempted != 0 or state().unk2C03 == 0) {
            c.func_8009E5C4(@as(c_int, slot.unk4.unk0.targetActor));
        }
        // the first hit's target is not bounded by the C
        if (slot.unk4C[0].unk40 == 0 and
            psx.get(psx.at(v.g.vs_battle_actors(), slot.unk4C[0].unk0.targetActor).*.?.stats).?.currentHP == 0)
        {
            c.func_8009E5C4(@as(c_int, slot.unk4.unk0.targetActor));
        }
    }
    if (v.g.D_800F1868().* & 3 == 0) {
        c.func_8006FD1C();
    }
}
