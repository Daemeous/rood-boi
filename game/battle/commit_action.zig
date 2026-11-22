//! The action committed: its costs and hits, the scores, phase 4 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// vs_battle_actionState, read at each use as the C does.
fn state() *v.vs_battle_ActionState {
    return v.g.vs_battle_actionState().*.?;
}

/// orig: vs_battle_commitSelectedAction (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Commits the chosen action (`kind`: 0, 2 or another selects its setup). A menu choice left pending (unk298C
/// 0xFF) with `kind` nonzero is dropped instead: back to the actor's turn (vs_battle_beginPostActionRecovery) for a targeted or plain
/// action, or vs_battle_returnToBattleActionMenu. Otherwise: link 0, the ability menu's item use counted down (the player's action), the
/// setup, the hits (vs_battle_resolveActionTargets), the weapon's attack count and the heal count scored, the zoom kind from the
/// action, phase 4, every other actor told, and the screen and sound state for the action.
pub fn commitAction(kind: c_int) callconv(.c) void {
    // ROOD_PC: the dev RPC action captures the state the menus leave here (pc/src/action_driver.c).
    c.pc_action_capture(@ptrCast(state()), kind, @bitCast(v.g.D_800F196C().*), @as(c_int, @as(*const u8, @ptrCast(v.g.vs_battle_menuState())).*));
    c.vs_field_storePreferredActionCameraDistance();
    const pending = state().unk298C;
    c.func_800C16DC();
    c.func_800C06E0();
    c.func_800C0738();
    c.func_800C05B4();
    if (kind != 0) {
        if (pending == 0xFF) {
            if (state().unk8.primaryTarget.target.targetActor != 0) {
                c.vs_menu_frameworkReleaseMenuRows();
                c.func_800E4C64(@as(c_int, state().unk8.primaryTarget.target.targetActor));
                state().unk8.actionIndex = 0;
                c.vs_battle_beginPostActionRecovery(0);
                return;
            }
            // the action index is not bounded by the C
            if (psx.getBits(psx.at(v.g.actions(), state().unk8.actionIndex), "unk2_0") == 0) {
                c.vs_menu_frameworkReleaseMenuRows();
                state().unk8.actionIndex = 0;
                c.vs_battle_beginPostActionRecovery(0);
                return;
            }
            state().unk8.actionIndex = 0;
            c.vs_battle_returnToBattleActionMenu();
            return;
        }
    }
    state().unk4 = 1;
    state().unk0 = 0;
    if (state().unk8.primaryTarget.target.targetActor == 0) {
        // ROOD_PC guard: the ability menu state is null until the menu first opens; the PS1 reads address 0.
        const ability: *align(1) v.D_800F18EC_t = if (psx.Global(?*v.D_800F18EC_t, "D_800F18EC").ptr().*) |p| p else psx.nullObject(v.D_800F18EC_t);
        if (ability.unk2 != 7) {
            if (ability.unk2 == 8) {
                _ = c.vs_battle_decreaseMiscCount(@as(c_int, ability.executeAbility.u16[1]));
            }
        }
        ability.unk2 = 0;
    } else {
        c.vs_battle_aiDisengageActorById(@as(c_int, state().unk8.primaryTarget.target.targetActor));
    }
    c.vs_menu_frameworkReleaseMenuRows();
    if (kind == 0) {
        c.vs_battle_collectEnvironmentActionTargets();
    } else if (kind == 2) {
        c.vs_battle_setAllActionTargets();
    } else {
        c.vs_battle_setSingleActionTarget();
    }
    c.vs_battle_resolveActionTargets(@as(c_int, state().unk8.actionIndex), @ptrCast(&state().queue[0]), @ptrCast(&state().unk8), 1);

    if (state().unk8.primaryTarget.target.targetActor == 0) {
        const index: c_int = state().unk8.actionIndex;
        if ((index == 1 or psx.conv(c_uint, index -% 0xB8) < 0x28) and state().unk8.targets[0].unk40 == 0 and
            // the target is not bounded by the C
            psx.at(v.g.vs_battle_actors(), state().unk8.targets[0].target.targetActor).*.?.unk27 != 0x80)
        {
            const attacks = &v.g.scoreData().weaponAttacks;
            if (v.g.vs_battle_characterState().*.?.equippedWeaponCategory == 0xA) {
                if (attacks[0] <= 0xFFFE) {
                    attacks[0] += 1; // <= 0xFFFE: no wrap
                }
            } else {
                // the category is not bounded by the C
                const count = psx.at(attacks, v.g.vs_battle_characterState().*.?.equippedWeaponCategory);
                if (count.* <= 0xFFFE) {
                    count.* += 1; // <= 0xFFFE: no wrap
                }
            }
        }
    }

    if (c.vs_battle_actionTargetDistance() >= 0x1C1 and psx.getBits(psx.at(v.g.actions(), state().unk8.actionIndex), "flagsD_4") == 1) {
        state().unk2C0E = 0;
    } else {
        state().unk2C0E = psx.getBits(psx.at(v.g.actions(), state().unk8.actionIndex), "flagsD_4");
    }
    state().unk2C02 = 0;
    state().previousChainInput = 0;
    state().chainAbilitySlot = 0;
    state().unk2C07 = 0;

    if (state().unk8.actionIndex >= 55) {
        psx.Global(u8, "D_8005FFAF").ptr().* = 0;
        switch (state().unk8.actionIndex) {
            0x4D, 0x60, 0xE0, 0xE1, 0xE2, 0xE3, 0xE8, 0xE9, 0xEA, 0xEB => {
                const score = v.g.scoreData();
                if (score.healCount < 0x3E8) {
                    score.healCount += 1; // < 0x3E8: no wrap
                }
            },
            else => {},
        }
    }

    v.g._cameraMode().* = 4;
    var i: c_int = 0;
    while (i < 16) : (i += 1) {
        if (i != state().unk8.primaryTarget.target.targetActor) {
            c.func_8009E5C4(i);
        }
    }
    c.vs_main_setClutState(1);
    c.vs_render_transformSceneColors(8, 4, 0, 0, 0);
    c.vs_actors_freezeAllActorAnimations();
    c.vs_battle_setActorMarkersHidden(1);
    _ = c.vs_field_setMinimapVisible(0);
    c.vs_field_setRoomAnimationsPaused(0);
}
