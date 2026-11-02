//! The first-person look around (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

const StickLook = *const fn ([*c]c_int, [*c]c_int) callconv(.c) void;

/// FP_INVERT_Y: the PC's inverted first-person pitch setting (host, read at each use as the C does).
fn invertY() bool {
    return psx.global(c_int, "pc_qol_fp_invert_y").* != 0;
}

/// orig: func_8006EC7C (src/BATTLE/BATTLE.PRG/146C.c)
/// Turns the first-person view: the D-pad by 32 (pitch inverted with FP_INVERT_Y), an analog pad's right stick
/// past its dead zone by a third of the excess, and the PC's right stick (pc_camera_stick_first_person); pitch kept
/// within +-896. Then actor 0 faces the yaw and the camera sits at its eyes (0xB4 up), looking along the angles.
pub fn func_8006EC7C() callconv(.c) void {
    var actor: v.func_8006EBF8_t = undefined;
    const camera = &v.g.vs_battle_cameraCurrentSpherical().values;
    const held = v.g.vs_main_buttonsPreviousState();
    if (held.* & @as(c_int, if (invertY()) 1 << 14 else 1 << 12) != 0) {
        camera.pitch = @rem(camera.pitch -% 32, 4096);
    }
    if (held.* & @as(c_int, if (invertY()) 1 << 12 else 1 << 14) != 0) {
        camera.pitch = @rem(camera.pitch +% 32, 4096);
    }
    if (held.* & (1 << 15) != 0) {
        camera.yaw = @rem(camera.yaw -% 32, 4096);
    }
    if (held.* & (1 << 13) != 0) {
        camera.yaw = @rem(camera.yaw +% 32, 4096);
    }
    const port = &v.g._portInfo()[0];
    if (port.mode == 7) {
        var x: c_int = @as(c_int, port.rStickX) - 128;
        var y: c_int = @as(c_int, port.rStickY) - 128;
        if (@as(c_uint, port.rStickX) -% 64 > 128) {
            if (x <= 0) {
                x = @as(c_int, port.rStickX) - 64;
            } else {
                x = @as(c_int, port.rStickX) - 192;
            }
            camera.yaw = @rem(camera.yaw +% @divTrunc(x, 3), 4096);
        }
        if (psx.conv(c_uint, y + 64) > 128) {
            if (y <= 0) {
                y += 64;
            } else {
                y -= 64;
            }
            camera.pitch = @rem(camera.pitch +% (if (invertY()) -@divTrunc(y, 3) else @divTrunc(y, 3)), 4096);
        }
    }
    // ROOD_PC: the right stick looks around (pc/src/camera_stick.c).
    if (psx.weak(StickLook, "pc_camera_stick_first_person")) |look| {
        var yaw: c_int = undefined;
        var pitch: c_int = undefined;
        look(&yaw, &pitch);
        camera.yaw = @rem(camera.yaw +% yaw, 4096);
        camera.pitch = @rem(camera.pitch +% (if (invertY()) -%pitch else pitch), 4096);
    }
    if (camera.pitch > 896) {
        camera.pitch = 896;
    } else if (camera.pitch < -896) {
        camera.pitch = -896;
    }
    c.func_800AA850(0, psx.conv(u16, camera.yaw & 0xFFF), 0);
    _ = c.func_800A1108(0, &actor);
    const position = &v.g.vs_scratch().camera.position;
    position.vx = @as(c_int, actor.unk0.unk4.vx) *% 4096;
    position.vz = @as(c_int, actor.unk0.unk4.vz) *% 4096;
    position.vy = (@as(c_int, actor.unk0.unk4.vy) -% 180) *% 4096;
    c._setCameraLookAtFromAngles(@ptrCast(camera));
}
