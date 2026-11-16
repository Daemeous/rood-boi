//! The battle camera's stick, d-pad and angle input (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

const AnalogMove = *const fn (*c_int, *c_int, *c_int) callconv(.c) c_int;

/// orig: vs_actors_updateAshleyMovementInput (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Reads the camera input and steers the camera by the stick, the d-pad or the buttons.
pub fn vs_actors_updateAshleyMovementInput() callconv(.c) c_int {
    var unksp10: v.func_800A1108_t = undefined;
    _ = c.vs_actors_actorPlacement(0, &unksp10);
    c.vs_field_updateCameraTileConstraint(0, @ptrCast(&unksp10));
    const temp_s0 = psx.get(v.g.vs_battle_characterState().*.?.stats);
    const temp_s1: c_uint = c.vs_actors_actorStateFlags(0);

    const step = v.g.vs_battle_screenTransitionStep().*;
    if ((step -% 1) < 2 or step == 4) {
        if (psx.getBits(v.g.vs_field_pendingDestinationDoor(), "unk6_15") != 0) {
            c.vs_actors_setDirectMovement(
                0,
                @as(c_int, psx.getBits(v.g.vs_field_pendingDestinationDoor(), "unk4_13")) << 10,
                @divTrunc(@as(c_int, temp_s0.?.unk31) *% 4096, 2),
            );
        } else {
            c.vs_actors_setDirectMovement(0, 0, 0);
        }
    } else {
        c.vs_field_updateCameraOrbitInput();

        const pad = v.g.D_800F1A50();
        pad[3] = 0;
        pad[1] = 0;

        if (v.g._portInfo()[0].mode == 7) {
            const port = &v.g._portInfo()[0];
            var temp_a1: c_int = @as(c_int, port.rStickX) -% 128;
            var temp_a0: c_int = @as(c_int, port.rStickY) -% 128;

            if (psx.conv(c_uint, @as(c_int, port.rStickX) -% 64) > 128 or temp_a0 < -64 or temp_a0 > 64) {
                if (temp_a1 > 64) {
                    temp_a1 -%= 64;
                } else if (temp_a1 < -64) {
                    temp_a1 +%= 64;
                } else {
                    temp_a1 = 0;
                }

                if (temp_a0 > 64) {
                    temp_a0 -%= 64;
                } else if (temp_a0 < -64) {
                    temp_a0 +%= 64;
                } else {
                    temp_a0 = 0;
                }
                pad[3] = 0;
                pad[1] = 0 -% temp_a1;
                pad[3] = temp_a0;
            }
        }

        if (pad[1] == 0 and pad[3] == 0) {
            if ((v.g.vs_main_buttonsPreviousState().* & (1 << 14)) != 0) {
                pad[1] = 0;
                pad[3] = 16;
            }
            if ((v.g.vs_main_buttonsPreviousState().* & (1 << 12)) != 0) {
                pad[1] +%= 0;
                pad[3] -%= 16;
            }
            if ((v.g.vs_main_buttonsPreviousState().* & (1 << 15)) != 0) {
                pad[3] +%= 0;
                pad[1] +%= 16;
            }
            if ((v.g.vs_main_buttonsPreviousState().* & (1 << 13)) != 0) {
                pad[3] +%= 0;
                pad[1] -%= 16;
            }
        }

        // PC: the left stick's own heading and pace (pc/src/analog_move.c, the setting analog_move, off by default).
        var analog_walk = false; // the walking pace: the one R2 held gives (stats unk31); the D-pad's own is the run
        if (psx.weak(AnalogMove, "pc_analog_move_get")) |analog| {
            var ax: c_int = 0;
            var az: c_int = 0;
            var run: c_int = 0;
            if (analog(&ax, &az, &run) != 0) {
                pad[1] = psx.conv(@TypeOf(pad[1]), ax);
                pad[3] = psx.conv(@TypeOf(pad[3]), az);
                analog_walk = run == 0;
            }
        }

        const cam = v.g.vs_battle_cameraCurrentSpherical();
        if (cam.values.distance > 2304) {
            cam.values.distance = 2304;
        } else if (cam.values.distance < 1536) {
            cam.values.distance = 1536;
        }

        if (pad[1] != 0 or pad[3] != 0) {
            // ratan2 is called once on every path of this block, with the same arguments.
            const angle: c_int = (c.ratan2(pad[1], pad[3]) +% cam.values.yaw +% 2048) & 0xFFF;
            if ((temp_s1 & 0x80000) != 0) {
                c.vs_actors_setDirectMovement(0, angle, @as(c_int, temp_s0.?.unk31) *% 4096);
            } else if ((v.g.vs_main_buttonsPreviousState().* & (1 << 1)) != 0 or analog_walk) {
                if ((v.g.vs_main_buttonsPressed().all & (1 << 7)) != 0) {
                    c.vs_actors_beginActorJumpMovement(0, angle, @as(c_int, temp_s0.?.unk31) *% 4096);
                } else {
                    c.vs_actors_setDirectMovement(0, angle, @as(c_int, temp_s0.?.unk31) *% 4096);
                }
            } else if ((v.g.vs_main_buttonsPressed().all & (1 << 7)) != 0) {
                c.vs_actors_beginActorJumpMovement(0, angle, @as(c_int, temp_s0.?.unk33) *% 4096);
            } else {
                c.vs_actors_setDirectMovement(0, angle, @as(c_int, temp_s0.?.unk33) *% 4096);
            }
        } else if ((temp_s1 & 0x80000) == 0 and (v.g.vs_main_buttonsPressed().all & (1 << 7)) != 0) {
            c.vs_actors_beginActorJumpMovement(0, (c.ratan2(0, 0) +% cam.values.yaw +% 2048) & 0xFFF, 0);
        } else {
            c.vs_actors_setDirectMovement(0, c.ratan2(0, 0) & 0xFFF, 0);
        }
    }

    return 0;
}
