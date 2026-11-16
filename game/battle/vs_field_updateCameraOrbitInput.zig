//! Battle camera: the right-stick and L1/R1 turn and zoom state (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// CAMERA_INPUT_LOCKED: the camera ignores turn and zoom input in these battle states.
fn cameraInputLocked() bool {
    const phase = v.g.D_800F196C().*;
    const mode = v.g._cameraMode().*;
    return phase == 3 or (phase == 2 and (mode == 4 or mode == 11));
}

/// orig: vs_field_updateCameraOrbitInput (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Advances the battle camera one frame: turns it, zooms it and eases its distance.
pub fn vs_field_updateCameraOrbitInput() callconv(.c) void {
    const cam = v.g.vs_battle_cameraCurrentSpherical();

    // ROOD_PC: right-stick camera (pc/src/camera_stick.c), continuous yaw and zoom.
    const Tick = *const fn (*anyopaque, c_int) callconv(.c) void;
    if (psx.weak(Tick, "pc_camera_stick_tick")) |tick| {
        tick(cam, @as(c_int, @intFromBool(cameraInputLocked())));
    }

    if (cam.delta.yaw == 0) {
        if (!cameraInputLocked()) {
            const held = v.g.vs_main_buttonsPreviousState().* & 0xC;
            if (held == 8) {
                cam.delta.yaw = 1;
                cam.delta.pitch = (cam.values.yaw -% 0x200) & ~@as(i32, 0x1FF);
            } else if (held == 4) {
                cam.delta.yaw = 2;
                cam.delta.pitch = (cam.values.yaw +% 0x200) & ~@as(i32, 0x1FF);
            }

            // ROOD_PC: from a yaw the right stick left between the 45-degree lines, R1 turns to the nearest line.
            const Turn = *const fn (*anyopaque) callconv(.c) void;
            if (psx.weak(Turn, "pc_camera_stick_turn")) |turn| {
                if (cam.delta.yaw != 0) turn(cam);
            }
        }
    } else {
        switch (cam.delta.yaw) {
            1 => {
                cam.values.yaw -%= cam.delta.distance;
                if (cam.values.yaw <= cam.delta.pitch +% 0x78) {
                    if ((v.g.vs_main_buttonsPreviousState().* & 0xC) == 8 and !cameraInputLocked()) {
                        cam.delta.pitch = (cam.delta.pitch -% 0x200) & ~@as(i32, 0x1FF);
                    }
                    if (cam.delta.distance > 8) {
                        cam.delta.distance -%= 8;
                    }
                } else if (cam.delta.distance < 40) {
                    cam.delta.distance +%= 8;
                    if (cam.delta.distance > 40) {
                        cam.delta.distance = 40;
                    }
                }
                if (cam.values.yaw <= cam.delta.pitch) {
                    cam.values.yaw &= 0xE00;
                    cam.delta.yaw = 0;
                }
            },
            2 => {
                cam.values.yaw +%= cam.delta.distance;
                if (cam.values.yaw >= cam.delta.pitch -% 0x78) {
                    if ((v.g.vs_main_buttonsPreviousState().* & 0xC) == 4 and !cameraInputLocked()) {
                        cam.delta.pitch = (cam.delta.pitch +% 0x200) & ~@as(i32, 0x1FF);
                    }
                    if (cam.delta.distance > 8) {
                        cam.delta.distance -%= 8;
                    }
                } else if (cam.delta.distance < 40) {
                    cam.delta.distance +%= 8;
                    if (cam.delta.distance > 40) {
                        cam.delta.distance = 40;
                    }
                }
                if (cam.values.yaw >= cam.delta.pitch) {
                    cam.values.yaw &= 0xE00;
                    cam.delta.yaw = 0;
                }
            },
            else => {},
        }
    }

    if ((v.g.vs_main_buttonsPressed().all & 0x100) != 0) {
        if (!cameraInputLocked()) {
            switch (cam.delta.mode) {
                0, 1 => {
                    cam.delta.mode = 2;
                },
                2, 3 => {
                    cam.delta.mode = 0;
                },
                else => {},
            }
        }
        switch (cam.delta.mode) {
            0, 1 => {
                cam.initialDistance = 0x600;
            },
            2, 3 => {
                cam.initialDistance = 0x900;
            },
            else => {},
        }
    }

    switch (cam.delta.mode) {
        0 => {
            if (cam.values.distance <= 0x600) {
                cam.values.distance = 0x600;
                cam.delta.mode = 1;
                return;
            }
            if (cam.values.distance <= cam.initialDistance +% 0xA0) {
                if (cam.unk24 < -16) {
                    cam.unk24 +%= 16;
                } else {
                    cam.unk24 = -16;
                }
            } else if (cam.unk24 >= -63) {
                cam.unk24 -%= 16;
                if (cam.unk24 < -64) {
                    cam.unk24 = -64;
                }
            }
            if (cam.values.distance > cam.initialDistance) {
                cam.values.distance +%= cam.unk24;
            }
        },
        1 => {},
        2 => {
            if (cam.values.distance >= 0x900) {
                cam.values.distance = 0x900;
                cam.delta.mode = 3;
                return;
            }
            if (cam.values.distance >= cam.initialDistance -% 0xA0) {
                if (cam.unk24 > 16) {
                    cam.unk24 -%= 16;
                } else {
                    cam.unk24 = 16;
                }
            } else if (cam.unk24 < 64) {
                cam.unk24 +%= 16;
                if (cam.unk24 > 64) {
                    cam.unk24 = 64;
                }
            }
            if (cam.values.distance < cam.initialDistance) {
                cam.values.distance +%= cam.unk24;
            }
        },
        else => {},
    }
}
