//! Battle's main loop (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");
const sprite_item = common.sprite_item;

const Site = *const fn (c_int, ?*anyopaque, c_int, c_int, ?*anyopaque, ?*anyopaque) callconv(.c) void;
const Anchor = *const fn (?*const anyopaque, ?*const anyopaque, c_int) callconv(.c) void;
const StickMenu = *const fn (?*anyopaque, c_int, c_int, c_int) callconv(.c) c_int;

/// The packet cursor (PC_CLASSIC's first and end).
fn cursor() ?*anyopaque {
    return v.g.vs_scratch().unk0;
}

/// The playable character's stats record (`psx_get(vs_battle_characterState->unk3C)`).
fn characterStats() *v.vs_battle_actor2 {
    return psx.get(v.g.vs_battle_characterState().*.?.stats).?;
}

fn characterWeaponDrawn() c_int {
    return v.g.vs_battle_characterState().*.?.weaponDrawn & 1;
}

fn buttonsPressed() c_uint {
    return v.g.vs_main_buttonsPressed().all;
}

/// The screen transition has covered the screen (the loops' exit test).
fn transitionFinished() bool {
    const step = v.g.vs_battle_screenTransitionStep().*;
    return (step == 1 or step == 2) and v.g.vs_battle_screenTransitionAlpha().* == 255;
}

fn setCameraPosition() void {
    c._setCameraPositionFromAngles(psx.cptr(c._sphericalValues, &v.g.vs_battle_cameraCurrentSpherical().values));
}

/// orig: vs_battle_exec (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Battle's main loop: one frame per pass of the innermost loop, never returns.
pub fn battleExec() callconv(.c) void {
    var position: v.func_8006EBF8_t = undefined;
    // Read when a screen transition runs without being assigned there (uninitialised in the
    // original): it keeps the previous pass's value.
    var skipCameraUpdate: c_int = 0;
    var i: c_int = undefined;

    v.g.vs_field_isMenuRequestQueued().* = 0;
    c.vs_boot_initializeGameplayOverlay(v.g.vs_main_startState().*);
    while (true) {
        c._finishLoadZnd(v.g.vs_field_pendingDestinationDoor().zndId);
        while (true) {
            c.vs_field_finishRoomLoad(v.g.vs_field_pendingDestinationDoor().mpdId);
            v.g.vs_field_isMenuRequestQueued().* = 0;
            while (true) {
                frame: while (true) {
                    // ROOD_PC: the cutscene viewer's story scenes (pc/src/story_boot.c).
                    c.pc_story_frame();
                    // ROOD_PC: QoL weapon slots (pc/src/weapon_slots.c): the pad read, then the L2 + R2 swap.
                    if (c.pc_weapon_slots_process_pad() == -1) {
                        break :frame;
                    }
                    const ot: [*c]c.u_long = @ptrCast(@alignCast(v.g.vs_render_legacyOrderingTables()[psx.conv(usize, v.g.vs_main_frameBuf().*)]));
                    psx.put(v.g.vs_render_legacyCurrentOrderingTable(), ot);
                    psx.put(&v.g.vs_scratch().unk8, ot + 2);
                    psx.put(&v.g.vs_scratch().unk4, ot + 18);
                    c.pc_frame_table_clear(ot, 0x822, 1);
                    // ROOD_PC: the frame's typed draw items (the effects') start empty with its table.
                    c.pc_frame_begin();
                    psx.put(&v.g.vs_scratch().unk0, v.g.vs_render_legacyPrimitiveBuffers()[psx.conv(usize, v.g.vs_main_frameBuf().*)]);
                    sprite_item.mask(ot, false);
                    if ((v.g.vs_main_startState().* == 3 or v.g.vs_main_startState().* == 4) and
                        (buttonsPressed() & 0x9FF) != 0)
                    {
                        c.vs_main_resetGame();
                    }
                    _ = c.vs_field_updateMovingPlatforms();
                    const status: c_uint = c.vs_actors_actorStateFlags(0);
                    _ = c.vs_actors_actorPlacement(0, &position);
                    // ROOD_PC: a held name window closed in time.
                    c.pc_charge_skip_name(0);

                    const step = v.g.vs_battle_screenTransitionStep().*;
                    if (step != 1 and step != 2 and step != 4) {
                        skipCameraUpdate = 0;
                        mode: switch (v.g.D_800F196C().*) {
                            0 => {},

                            1 => {
                                if ((status & 0x2000) != 0) {
                                    break :mode;
                                }
                                if (c.vs_event_evaluateRoomEventTriggers(0x20, 0, 0, 0) == 1) {
                                    c.func_80073898();
                                    break :mode;
                                }
                                // ROOD_PC: the editor's Room Browser: a requested warp starts here, as a door would (pc/src/room_warp.c).
                                if (c.pc_room_warp_poll() != 0) {
                                    break :mode;
                                }
                                // ROOD_PC: the dev RPC action starts a player's action here (pc/src/action_driver.c).
                                if (c.pc_action_poll() != 0) {
                                    break :mode;
                                }
                                if (c.vs_field_checkAshleyTileTriggers(0, @ptrCast(&position)) != 0) {
                                    break :mode;
                                }
                                if (characterStats().actionRecoveryTimer == 0 and (status & 0x4000) == 0 and
                                    v.g.vs_field_isMenuRequestQueued().* == 0 and (v.g.vs_main_buttonsPreviousState().* & 1) != 0)
                                {
                                    c.vs_field_beginShortcutMenu();
                                    break :mode;
                                }
                                if (v.g.vs_field_isMenuRequestQueued().* != 0) {
                                    if (characterStats().actionRecoveryTimer == 0 and (status & 0x4000) == 0 and
                                        c.vs_menu_frameworkOpenGameMenu(0, 0) != 0)
                                    {
                                        c.vs_field_beginBattleActionMenu(0, 0);
                                        v.g.vs_field_isMenuRequestQueued().* = 0;
                                        break :mode;
                                    }
                                } else if ((buttonsPressed() & 0x10) != 0 and (v.g.vs_main_buttonsPreviousState().* & 1) == 0) {
                                    if (characterStats().actionRecoveryTimer == 0 and (status & 0x4000) == 0) {
                                        if (c.vs_menu_frameworkOpenGameMenu(0, 0) != 0) {
                                            c.vs_field_beginBattleActionMenu(0, 0);
                                            break :mode;
                                        }
                                    } else {
                                        v.g.vs_field_isMenuRequestQueued().* = 1;
                                    }
                                }
                                if ((buttonsPressed() & 0x40) != 0) {
                                    if (c.vs_field_findInteractableGimmickAhead(0) >= 0) {
                                        if (characterWeaponDrawn() == 0) {
                                            _ = c.vs_field_beginGimmickInteraction(0);
                                            break :mode;
                                        }
                                    } else if ((status & 0x3F4000) == 0) {
                                        i = c.vs_actors_findNearestActorAhead(0, 0xA0, 0x80);
                                        if (i >= 0 and psx.at(v.g.vs_battle_actors(), i).*.?.unk24 == 4 and
                                            c.vs_event_evaluateRoomEventTriggers(4, 0, i, 0) == 1)
                                        {
                                            c.func_80073898();
                                            break :mode;
                                        }
                                        if (c.vs_field_tryTriggerFacingTileEvent(0) != 0) {
                                            c.func_80073898();
                                            break :mode;
                                        }
                                        const room_object = c.vs_field_findFacingRoomObject(0);
                                        if (room_object != null) {
                                            if (c.vs_field_interactWithLockedRoomObject(room_object) == 0) {
                                                c.vs_field_beginObjectInteractionWait();
                                                break :mode;
                                            }
                                        } else {
                                            const chest = c.vs_field_findFacingUnlootedChest(0);
                                            if (chest != null) {
                                                c.vs_field_beginChestInteraction(@ptrCast(@alignCast(chest)), 0);
                                                break :mode;
                                            }
                                        }
                                    }
                                }
                                if ((status & 0x4000) == 0) {
                                    if ((status & 0x180000) == 0 and (buttonsPressed() & 0xC00) != 0) {
                                        c._initBattleCameraTransition();
                                        break :mode;
                                    }
                                    if ((buttonsPressed() & 0x20) != 0) {
                                        if (characterWeaponDrawn() != 0) {
                                            if (characterStats().actionRecoveryTimer == 0 and (characterStats().statuses & 0x2000) == 0) {
                                                c.vs_main_playSfxDefault(0x7E, 9);
                                                c.vs_battle_beginActorActionSelection(0, 1);
                                                skipCameraUpdate = 1;
                                                break :mode;
                                            }
                                            c.vs_main_playSfxDefault(0x7E, 0xC);
                                        } else if ((status & 0x180000) == 0) {
                                            c.vs_battle_beginAshleyWeaponToggle();
                                            break :mode;
                                        }
                                    }
                                    if ((buttonsPressed() & 0x40) != 0 and characterWeaponDrawn() != 0) {
                                        c.vs_battle_beginAshleyWeaponToggle();
                                        break :mode;
                                    }
                                }
                                if (c.vs_actors_updateAshleyMovementInput() != 0) {
                                    break :mode;
                                }
                                switch (c.vs_actors_executeQueuedActorCommands()) {
                                    1 => c.func_80073898(),
                                    2 => {
                                        v.g.D_800F18FC().* = 0;
                                        c.vs_items_openPendingEnemyLoot();
                                    },
                                    else => {},
                                }
                            },

                            2 => c.vs_field_trackActionCameraTarget(0),

                            3 => {
                                if (v.g._cameraMode().* == 1) {
                                    c.vs_field_updateCameraOrbitInput();
                                    c.vs_actors_updateRoomActorMovement();
                                    setCameraPosition();
                                    if (v.g.vs_battle_cameraCurrentSpherical().delta.yaw != 0) {
                                        break :mode;
                                    }
                                    if (v.g.vs_battle_cameraCurrentSpherical().delta.mode == 1 or
                                        v.g.vs_battle_cameraCurrentSpherical().delta.mode == 3)
                                    {
                                        var ready: c_int = 1;
                                        i = 0;
                                        while (i < 16) : (i +%= 1) {
                                            if (v.g.vs_battle_actors()[@intCast(i)] != null and
                                                (c.vs_actors_actorStateFlags(i) & 0x20004000) != 0)
                                            {
                                                ready = 0;
                                                break;
                                            }
                                        }
                                        if (ready != 0) {
                                            c.vs_event_startPreparedCutscene();
                                        }
                                    }
                                } else {
                                    c.vs_render_updateGimImageSet();
                                    _ = c.vs_actors_executeQueuedActorCommands();
                                    if (c.vs_event_evaluateRoomEventTriggers(0xFF, 0, 0, 0) == 0) {
                                        if (v.g.vs_main_startState().* == 3 or v.g.vs_main_startState().* == 4) {
                                            c.vs_main_resetGame();
                                        } else {
                                            c.vs_event_endFieldCutscene();
                                        }
                                    }
                                }
                            },

                            4 => {
                                if (c.func_800CEEBC() != 0) {
                                    _ = c.vs_effects_startEffectTrack(0);
                                }
                                if ((v.g.D_800F1868().* & 3) == 0) {
                                    c.func_80073484();
                                }
                            },

                            5 => {
                                if (v.g._cameraMode().* == 1) {
                                    c.vs_menu_frameworkUpdateMenuOverlay();
                                    const menu = c.vs_menu_frameworkActionSelectionState();
                                    psx.put(v.g.D_800F18EC(), menu);
                                    const view: *v.D_800F18EC_t = @ptrCast(@alignCast(menu.?));
                                    if (view.unk0 == 2) {
                                        c.vs_field_dispatchBattleActionMenuResult(menu);
                                    }
                                }
                            },

                            6 => {
                                if ((v.g.vs_main_buttonsPreviousState().* & 1) != 0) {
                                    c.vs_battle_showShortcutHud();
                                    var shortcut: c_int = undefined;
                                    if ((buttonsPressed() & 0x20) != 0) {
                                        shortcut = 1;
                                    } else if ((buttonsPressed() & 0x10) != 0) {
                                        shortcut = 2;
                                    } else if ((buttonsPressed() & 0x80) != 0) {
                                        shortcut = 3;
                                    } else if ((buttonsPressed() & 0x40) != 0) {
                                        shortcut = 4;
                                    } else if ((buttonsPressed() & 0x1000) != 0) {
                                        shortcut = 5;
                                    } else if ((buttonsPressed() & 0x4000) != 0) {
                                        shortcut = 6;
                                    } else if ((buttonsPressed() & 0x2000) != 0) {
                                        shortcut = 7;
                                    } else if ((buttonsPressed() & 0x8000) != 0) {
                                        shortcut = 8;
                                    } else {
                                        break :mode;
                                    }
                                    if (c.vs_battle_validateShortcutSelection(shortcut) != 0) {
                                        c.vs_field_beginBattleActionMenu(0, 0);
                                    }
                                } else {
                                    c.vs_field_endShortcutMenu();
                                }
                            },

                            7 => switch (v.g._cameraMode().*) {
                                0 => c.vs_field_updateFirstPersonViewEntry(),
                                1 => {
                                    c._checkFirstPersonViewExit();
                                    c.vs_field_updateFirstPersonCamera();
                                },
                                2 => {
                                    c.vs_field_updateFirstPersonViewExit();
                                    if ((buttonsPressed() & 0xC00) != 0) {
                                        c._initBattleCameraTransition();
                                    }
                                },
                                else => {},
                            },

                            8 => {
                                switch (v.g._cameraMode().*) {
                                    1 => {
                                        if (c.vs_field_stepTrapCameraFocus() == 0) {
                                            c.vs_field_dispatchTrapOrChestInteraction();
                                        }
                                    },
                                    2 => {
                                        if (c.func_80074294() == 0) {
                                            c.vs_field_finishTrapInteraction();
                                        }
                                    },
                                    4 => {
                                        _ = c.vs_field_trackInteractionCameraToAshley();
                                        if (c._isLookAtAtDestination() != 0) {
                                            _ = c.vs_battle_aiRebuildActorActionPlans(&v.g.vs_battle_actionState().*.?.queue[0]);
                                            c.vs_battle_beginPostActionRecovery(0);
                                            v.g.D_800F196C().* = 2;
                                        }
                                    },
                                    else => {},
                                }
                                setCameraPosition();
                            },

                            9 => {
                                switch (v.g._cameraMode().*) {
                                    1 => {
                                        if (c.vs_field_waitForChestCameraFocus() == 0) {
                                            c.vs_field_dispatchTrapOrChestInteraction();
                                        }
                                    },
                                    2 => {
                                        if (c.func_8007424C() != 0) {
                                            c.vs_field_openChestContents();
                                        }
                                    },
                                    3 => {
                                        if (c.vs_field_updateChestContentsSelection() == 0) {
                                            c.func_800742A4();
                                        }
                                    },
                                    4 => {
                                        const ready = c.vs_field_trackInteractionCameraToAshley();
                                        if (c._isLookAtAtDestination() != 0 and ready != 0) {
                                            c.func_800CB50C();
                                            c.vs_battle_beginPostActionRecovery(0);
                                            v.g.D_800F196C().* = 2;
                                        }
                                    },
                                    else => {},
                                }
                                setCameraPosition();
                            },

                            10 => {
                                c.vs_actors_setDirectMovement(0, 0, 0);
                                if (c.vs_actors_isActorActionAnimationBusy(0) == 0) {
                                    c.func_8007350C();
                                }
                            },

                            11 => {
                                if (c.func_800CB45C() == 0) {
                                    c.vs_field_endObjectInteractionWait();
                                }
                            },

                            12 => {
                                if (c.vs_field_areGimmicksMoving() == 0) {
                                    c.func_8006F5CC();
                                }
                            },

                            else => {},
                        }
                    } else {
                        if (v.g.D_800F196C().* == 3) {
                            c.vs_render_updateGimImageSet();
                        } else if ((status & 0x2000) == 0) {
                            _ = c.vs_actors_updateAshleyMovementInput();
                        }
                    }

                    _ = c.vs_event_updateEventPlayback();
                    switch (v.g.D_800F196C().*) {
                        1, 10, 12 => _ = c.vs_field_updateAshleyFollowCamera(),
                        2 => switch (v.g._cameraMode().*) {
                            7...10 => {},
                            else => setCameraPosition(),
                        },
                        else => {},
                    }
                    // ROOD_PC: right-stick camera in the menus that leave the scene in view (pc/src/camera_stick.c):
                    // the follow camera's own yaw and zoom, placed again when the stick moved them.
                    if (psx.weak(StickMenu, "pc_camera_stick_menu")) |stickMenu| {
                        if (stickMenu(&v.g.vs_battle_cameraCurrentSpherical().*, psx.conv(c_int, v.g.D_800F196C().*),
                            psx.conv(c_int, v.g._cameraMode().*), psx.conv(c_int, v.g.vs_battle_menuState().currentMenu)) != 0)
                        {
                            setCameraPosition();
                        }
                    }

                    c.vs_field_updateCameraFacingState();
                    c.vs_render_buildCameraViewMatrix();
                    common.gte.setFrame(&v.g.vs_view().viewMatrix);
                    if (skipCameraUpdate == 0) {
                        switch (v.g.D_800F196C().*) {
                            0, 1 => {
                                const transition = v.g.vs_battle_screenTransitionStep().*;
                                if (transition != 1 and transition != 2 and transition != 4) {
                                    switch (c.vs_battle_updateRoomActorVitals()) {
                                        1 => c.func_80073898(),
                                        2 => {
                                            v.g.D_800F18FC().* = 0;
                                            c.vs_items_openPendingEnemyLoot();
                                        },
                                        else => {},
                                    }
                                }
                            },
                            2 => {
                                c.func_80088B8C();
                                camera: switch (v.g._cameraMode().*) {
                                    1 => c.func_80078248(),
                                    2 => c.func_800782E4(),
                                    3 => c.func_80078364(),
                                    4 => {
                                        // ROOD_PC: the charging skip (pc/src/charge_skip.inc).
                                        if (c.pc_charge_skip(4) != 0) break :camera;
                                        c._handleCombatCameraZoom();
                                    },
                                    5 => {
                                        if (c.pc_charge_skip(5) != 0) break :camera;
                                        c.func_80078748();
                                    },
                                    6 => c.vs_battle_startReadyActionAnimation(),
                                    7 => c.vs_battle_updateBattleAbilityTiming(),
                                    8 => c.func_80079030(),
                                    9 => c.func_80079050(),
                                    10 => c.func_800790BC(),
                                    11 => c.vs_battle_finishActionSequence(),
                                    12 => c.vs_battle_updateLootResults(),
                                    13 => c.vs_battle_updateAbilityUnlockDelay(),
                                    14 => c.vs_battle_updateAbilityUnlockResults(),
                                    else => {},
                                }
                                if (v.g._cameraMode().* < 4) {
                                    if (v.g._cameraMode().* != 0) {
                                        c.func_800C0D78();
                                    }
                                }
                            },
                            else => {},
                        }
                    }

                    if (v.g.vs_field_isFieldRenderingEnabled().* != 0) {
                        c.vs_field_drawRoomBackground(null);
                        c.vs_field_drawRoomScene();
                        c.vs_field_advanceRoomAnimationClock();
                        c.SetGeomOffset(160, 112);
                        if (v.g.vs_field_isFieldRenderingEnabled().* != 0) {
                            c.vs_actors_updateAndDrawRoomModels(psx.cptr(c.MATRIX, &v.g.vs_view().viewMatrix));
                            _ = c.vs_battle_processObjectDataQueue();
                            common.gte.setFrame(&v.g.vs_view().viewMatrix);
                            c.func_800941FC();
                        }
                    }
                    i = 0;
                    v.g.D_800F1868().* = c.vs_effects_updateEffectRuntime();
                    // ROOD_PC: PC_CLASSIC (classic_hook.h) around the status display, which widescreen anchors to the
                    // left edge (pc/src/gpu_hw.c).
                    {
                        const first = cursor();
                        const hud = psx.get(v.g.vs_scratch().unk0);
                        c.vs_battle_drawBattleHud();
                        if (psx.weak(Anchor, "pc_hw_anchor_packets")) |anchor| {
                            anchor(hud, psx.get(v.g.vs_scratch().unk0), 1);
                        }
                        if (psx.weak(Site, "pc_classic_site")) |site| site(46, null, 0, 0, first, cursor());
                    }
                    c.func_800CB83C();
                    while (i < @divTrunc(v.g.vs_gametime_tickspeed().*, 2)) : (i +%= 1) {
                        c.vs_main_setVibrateParams();
                        c.vs_render_updateBackgroundGradientTransition();
                        c.vs_render_updateModelTintTransitions();
                        c.vs_render_updateClutTransitions();
                    }
                    c._drawScreenTransition(ot);
                    c.vs_render_drawFramebufferEffect(ot);
                    if (v.g.D_800F196C().* == 1) {
                        c.func_800E511C();
                    } else if (v.g.D_800F196C().* == 3) {
                        c.vs_battle_aiUpdateActorNavigationOverrides();
                    }
                    sprite_item.mask(ot + 0x820, v.g.vs_render_framebufferEffectState().* == 1 or v.g.vs_render_framebufferEffectState().* == 2);
                    _ = c.vs_core_elapsedFrameCounterTicks();
                    _ = c.vs_render_presentFieldFrame(ot + 0x821);
                    c.vs_render_advanceTexturePageRetirements();
                    if (transitionFinished()) break :frame;
                }
                if (transitionFinished()) break;
            }
            c.vs_field_unloadCurrentRoom();
            if (v.g.vs_battle_screenTransitionStep().* == 2) break;
        }
        c.vs_field_releaseZoneData();
    }
}
