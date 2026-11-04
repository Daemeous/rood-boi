//! Battle's main loop (src/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

const Site = *const fn (c_int, ?*anyopaque, c_int, c_int, ?*anyopaque, ?*anyopaque) callconv(.c) void;
const Anchor = *const fn (?*const anyopaque, ?*const anyopaque, c_int) callconv(.c) void;
const StickMenu = *const fn (?*anyopaque, c_int, c_int, c_int) callconv(.c) c_int;

/// The scratchpad's packet cursor word as a guest pointer (PC_CLASSIC's first and end).
fn cursor() ?*anyopaque {
    const word: *const u32 = @ptrFromInt(0x1F800000);
    return @ptrFromInt(@as(usize, word.*));
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

/// orig: vs_battle_exec (src/BATTLE/BATTLE.PRG/146C.c)
/// Battle's main loop: one frame per pass of the innermost loop, never returns.
pub fn battleExec() callconv(.c) void {
    var position: v.func_8006EBF8_t = undefined;
    // Read when a screen transition runs without being assigned there (uninitialised in the
    // original): it keeps the previous pass's value.
    var skipCameraUpdate: c_int = 0;
    var i: c_int = undefined;

    v.g.D_800F1858().* = 0;
    c.func_800FA35C(v.g.vs_main_startState().*);
    while (true) {
        c._finishLoadZnd(v.g.D_800F1AB0().zndId);
        while (true) {
            c.func_80089DC0(v.g.D_800F1AB0().mpdId);
            v.g.D_800F1858().* = 0;
            while (true) {
                frame: while (true) {
                    // ROOD_PC: the cutscene viewer's story scenes (pc/src/story_boot.c).
                    c.pc_story_frame();
                    // ROOD_PC: QoL weapon slots (pc/src/weapon_slots.c): the pad read, then the L2 + R2 swap.
                    if (c.pc_weapon_slots_process_pad() == -1) {
                        break :frame;
                    }
                    const ot: [*c]c.u_long = @ptrCast(@alignCast(v.g.D_80055C80()[psx.conv(usize, v.g.vs_main_frameBuf().*)]));
                    psx.put(v.g.D_800F1870(), ot);
                    psx.put(&v.g.vs_scratch().unk8, ot + 2);
                    psx.put(&v.g.vs_scratch().unk4, ot + 18);
                    _ = c.ClearOTagR(ot, 0x822);
                    psx.put(&v.g.vs_scratch().unk0, v.g.D_8005E0C0()[psx.conv(usize, v.g.vs_main_frameBuf().*)]);
                    c.AddPrim(ot, psx.at(v.g.D_800F1970(), v.g.vs_main_frameBuf().*));
                    if ((v.g.vs_main_startState().* == 3 or v.g.vs_main_startState().* == 4) and
                        (buttonsPressed() & 0x9FF) != 0)
                    {
                        c.vs_main_resetGame();
                    }
                    _ = c.func_8008C8A8();
                    const status: c_uint = c.func_800A0BE0(0);
                    _ = c.func_800A1108(0, &position);
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
                                if (c.func_800BEC58(0x20, 0, 0, 0) == 1) {
                                    c.func_80073898();
                                    break :mode;
                                }
                                // ROOD_PC: the editor's Room Browser: a requested warp starts here, as a door would (pc/src/room_warp.c).
                                if (c.pc_room_warp_poll() != 0) {
                                    break :mode;
                                }
                                if (c.func_80074BAC(0, @ptrCast(&position)) != 0) {
                                    break :mode;
                                }
                                if (characterStats().unk954 == 0 and (status & 0x4000) == 0 and
                                    v.g.D_800F1858().* == 0 and (v.g.vs_main_buttonsPreviousState().* & 1) != 0)
                                {
                                    c.func_8007352C();
                                    break :mode;
                                }
                                if (v.g.D_800F1858().* != 0) {
                                    if (characterStats().unk954 == 0 and (status & 0x4000) == 0 and
                                        c.func_800CACD0(0, 0) != 0)
                                    {
                                        c.func_8007357C(0, 0);
                                        v.g.D_800F1858().* = 0;
                                        break :mode;
                                    }
                                } else if ((buttonsPressed() & 0x10) != 0 and (v.g.vs_main_buttonsPreviousState().* & 1) == 0) {
                                    if (characterStats().unk954 == 0 and (status & 0x4000) == 0) {
                                        if (c.func_800CACD0(0, 0) != 0) {
                                            c.func_8007357C(0, 0);
                                            break :mode;
                                        }
                                    } else {
                                        v.g.D_800F1858().* = 1;
                                    }
                                }
                                if ((buttonsPressed() & 0x40) != 0) {
                                    if (c.func_800A38E0(0) >= 0) {
                                        if (characterWeaponDrawn() == 0) {
                                            _ = c.func_800A3E6C(0);
                                            break :mode;
                                        }
                                    } else if ((status & 0x3F4000) == 0) {
                                        i = c.func_800A3760(0, 0xA0, 0x80);
                                        if (i >= 0 and psx.at(v.g.vs_battle_actors(), i).*.?.unk24 == 4 and
                                            c.func_800BEC58(4, 0, i, 0) == 1)
                                        {
                                            c.func_80073898();
                                            break :mode;
                                        }
                                        if (c.func_80074860(0) != 0) {
                                            c.func_80073898();
                                            break :mode;
                                        }
                                        var result: c_int = psx.conv(c_int, psx.addr(c.func_800748B8(0)));
                                        if (result != 0) {
                                            if (c.func_80073AFC(@ptrFromInt(@as(usize, psx.conv(u32, result)))) == 0) {
                                                c.func_80073AA4();
                                                break :mode;
                                            }
                                        } else {
                                            result = psx.conv(c_int, psx.addr(c.func_80074950(0)));
                                            if (result != 0) {
                                                c.func_80074050(@ptrFromInt(@as(usize, psx.conv(u32, result))), 0);
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
                                            if (characterStats().unk954 == 0 and (characterStats().statuses & 0x2000) == 0) {
                                                c.vs_main_playSfxDefault(0x7E, 9);
                                                c.func_80072EC4(0, 1);
                                                skipCameraUpdate = 1;
                                                break :mode;
                                            }
                                            c.vs_main_playSfxDefault(0x7E, 0xC);
                                        } else if ((status & 0x180000) == 0) {
                                            c.func_800734C0();
                                            break :mode;
                                        }
                                    }
                                    if ((buttonsPressed() & 0x40) != 0 and characterWeaponDrawn() != 0) {
                                        c.func_800734C0();
                                        break :mode;
                                    }
                                }
                                if (c.func_8006E7F0() != 0) {
                                    break :mode;
                                }
                                switch (c.func_80075554()) {
                                    1 => c.func_80073898(),
                                    2 => {
                                        v.g.D_800F18FC().* = 0;
                                        c.func_8006FB48();
                                    },
                                    else => {},
                                }
                            },

                            2 => c.func_8006E640(0),

                            3 => {
                                if (v.g._cameraMode().* == 1) {
                                    c.func_8006E158();
                                    c.func_800A4D8C();
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
                                                (c.func_800A0BE0(i) & 0x20004000) != 0)
                                            {
                                                ready = 0;
                                                break;
                                            }
                                        }
                                        if (ready != 0) {
                                            c.func_80073870();
                                        }
                                    }
                                } else {
                                    c.func_800C8778();
                                    _ = c.func_80075554();
                                    if (c.func_800BEC58(0xFF, 0, 0, 0) == 0) {
                                        if (v.g.vs_main_startState().* == 3 or v.g.vs_main_startState().* == 4) {
                                            c.vs_main_resetGame();
                                        } else {
                                            c.func_800738E4();
                                        }
                                    }
                                }
                            },

                            4 => {
                                if (c.func_800CEEBC() != 0) {
                                    _ = c.func_800CEF74(0);
                                }
                                if ((v.g.D_800F1868().* & 3) == 0) {
                                    c.func_80073484();
                                }
                            },

                            5 => {
                                if (v.g._cameraMode().* == 1) {
                                    c.func_800C97BC();
                                    const menu = c.func_800CB66C();
                                    psx.put(v.g.D_800F18EC(), menu);
                                    const view: *v.D_800F18EC_t = @ptrCast(@alignCast(menu.?));
                                    if (view.unk0 == 2) {
                                        c.func_800735F8(menu);
                                    }
                                }
                            },

                            6 => {
                                if ((v.g.vs_main_buttonsPreviousState().* & 1) != 0) {
                                    c.func_800C9F88();
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
                                        c.func_8007357C(0, 0);
                                    }
                                } else {
                                    c.func_80073554();
                                }
                            },

                            7 => switch (v.g._cameraMode().*) {
                                0 => c.func_80074580(),
                                1 => {
                                    c._checkFirstPersonViewExit();
                                    c.func_8006EC7C();
                                },
                                2 => {
                                    c.func_80074744();
                                    if ((buttonsPressed() & 0xC00) != 0) {
                                        c._initBattleCameraTransition();
                                    }
                                },
                                else => {},
                            },

                            8 => {
                                switch (v.g._cameraMode().*) {
                                    1 => {
                                        if (c.func_8007418C() == 0) {
                                            c.func_800741D4();
                                        }
                                    },
                                    2 => {
                                        if (c.func_80074294() == 0) {
                                            c.func_80074314();
                                        }
                                    },
                                    4 => {
                                        _ = c.func_80074374();
                                        if (c._isLookAtAtDestination() != 0) {
                                            _ = c.func_800DEC88(&v.g.D_800F19CC().*.?.unk854[0]);
                                            c.func_8006FBCC(0);
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
                                        if (c.func_80074120() == 0) {
                                            c.func_800741D4();
                                        }
                                    },
                                    2 => {
                                        if (c.func_8007424C() != 0) {
                                            c.func_800743E0();
                                        }
                                    },
                                    3 => {
                                        if (c.func_800744B8() == 0) {
                                            c.func_800742A4();
                                        }
                                    },
                                    4 => {
                                        const ready = c.func_80074374();
                                        if (c._isLookAtAtDestination() != 0 and ready != 0) {
                                            c.func_800CB50C();
                                            c.func_8006FBCC(0);
                                            v.g.D_800F196C().* = 2;
                                        }
                                    },
                                    else => {},
                                }
                                setCameraPosition();
                            },

                            10 => {
                                c.func_800B64A8(0, 0, 0);
                                if (c.func_8009E4B0(0) == 0) {
                                    c.func_8007350C();
                                }
                            },

                            11 => {
                                if (c.func_800CB45C() == 0) {
                                    c.func_80073ACC();
                                }
                            },

                            12 => {
                                if (c.func_800A47C4() == 0) {
                                    c.func_8006F5CC();
                                }
                            },

                            else => {},
                        }
                    } else {
                        if (v.g.D_800F196C().* == 3) {
                            c.func_800C8778();
                        } else if ((status & 0x2000) == 0) {
                            _ = c.func_8006E7F0();
                        }
                    }

                    _ = c.func_800BF850();
                    switch (v.g.D_800F196C().*) {
                        1, 10, 12 => _ = c.func_8006EBF8(),
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

                    c.func_80069D78();
                    c.func_8007ACB0();
                    c.SetRotMatrix(psx.cptr(c.MATRIX, &v.g.vs_scratch().viewMatrix));
                    c.SetTransMatrix(psx.cptr(c.MATRIX, &v.g.vs_scratch().viewMatrix));
                    if (skipCameraUpdate == 0) {
                        switch (v.g.D_800F196C().*) {
                            0, 1 => {
                                const transition = v.g.vs_battle_screenTransitionStep().*;
                                if (transition != 1 and transition != 2 and transition != 4) {
                                    switch (c.func_80088B6C()) {
                                        1 => c.func_80073898(),
                                        2 => {
                                            v.g.D_800F18FC().* = 0;
                                            c.func_8006FB48();
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
                                    6 => c.func_800787F0(),
                                    7 => c.func_80078AB4(),
                                    8 => c.func_80079030(),
                                    9 => c.func_80079050(),
                                    10 => c.func_800790BC(),
                                    11 => c.func_800793C0(),
                                    12 => c.func_800797BC(),
                                    13 => c.func_8007980C(),
                                    14 => c.func_8007983C(),
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

                    if (v.g.D_800F190C().* != 0) {
                        c.func_8008EC48(null);
                        c.func_8008AC78();
                        c.func_8008B28C();
                        c.SetGeomOffset(160, 112);
                        if (v.g.D_800F190C().* != 0) {
                            c.func_800AEF94(psx.cptr(c.MATRIX, &v.g.vs_scratch().viewMatrix));
                            _ = c.vs_battle_processObjectDataQueue();
                            c.SetRotMatrix(psx.cptr(c.MATRIX, &v.g.vs_scratch().viewMatrix));
                            c.SetTransMatrix(psx.cptr(c.MATRIX, &v.g.vs_scratch().viewMatrix));
                            c.func_800941FC();
                        }
                        // ROOD_PC: explicit rendering diagnostic; normal battle logic is unchanged.
                        if (psx.global(c_int, "pc_room_checkpoint_requested").* != 0) {
                            // Inspect loaded geometry even while an entry script hides it.
                            if (v.g.vs_battle_roomData().unk1 == 0 and psx.get(v.g.vs_battle_roomData().geometrySection) != null) {
                                c.func_8008B1FC(psx.get(v.g.vs_battle_roomData().geometrySection));
                            }
                            _ = c.PutDrawEnv(psx.cptr(c.DRAWENV, psx.at(v.g.vs_main_drawEnv(), @intFromBool(v.g.vs_main_frameBuf().* == 0))));
                            c.vs_main_commitClut();
                            c.DrawOTag(ot + 0x821);
                            c.pc_finish_room_checkpoint();
                        }
                    }
                    i = 0;
                    v.g.D_800F1868().* = c.func_800CF060();
                    // ROOD_PC: PC_CLASSIC (classic_hook.h) around the status display, which widescreen anchors to the
                    // left edge (pc/src/gpu_hw.c).
                    {
                        const first = cursor();
                        const hud = psx.get(v.g.vs_scratch().unk0);
                        c.func_800CA2DC();
                        if (psx.weak(Anchor, "pc_hw_anchor_packets")) |anchor| {
                            anchor(hud, psx.get(v.g.vs_scratch().unk0), 1);
                        }
                        if (psx.weak(Site, "pc_classic_site")) |site| site(46, null, 0, 0, first, cursor());
                    }
                    c.func_800CB83C();
                    while (i < @divTrunc(v.g.vs_gametime_tickspeed().*, 2)) : (i +%= 1) {
                        c.vs_main_setVibrateParams();
                        c.func_80047FFC();
                        c.func_800481C0();
                        c.func_800483FC();
                    }
                    c._drawScreenTransition(ot);
                    c.func_8007D734(ot);
                    if (v.g.D_800F196C().* == 1) {
                        c.func_800E511C();
                    } else if (v.g.D_800F196C().* == 3) {
                        c.func_800E5308();
                    }
                    const stp = psx.at(v.g.D_800F1988(), v.g.vs_main_frameBuf().*);
                    if (v.g.D_800F1B98().* == 1 or v.g.D_800F1B98().* == 2) {
                        c.SetDrawStp(psx.cptr(c.DR_STP, stp), 1);
                        c.AddPrim(ot + 0x820, stp);
                    } else {
                        c.SetDrawStp(psx.cptr(c.DR_STP, stp), 0);
                        c.AddPrim(ot + 0x820, stp);
                    }
                    _ = c.func_800E45B4();
                    _ = c.func_8007629C(ot + 0x821);
                    c.func_8007DF98();
                    if (transitionFinished()) break :frame;
                }
                if (transitionFinished()) break;
            }
            c.func_8008A3A0();
            if (v.g.vs_battle_screenTransitionStep().* == 2) break;
        }
        c.func_80089098();
    }
}
