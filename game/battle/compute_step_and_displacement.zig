//! The camera's step toward a target point (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _computeStepAndDisplacement (src/BATTLE/BATTLE.PRG/146C.c)
/// The target (clamped to the zone when asked) less the camera's look-at is the displacement; its direction
/// (VectorNormal) times the camera speed is the step. The target counts as centred when it projects inside the
/// screen box (x 61..259, y 61..179) and was not clamped: then the speed falls by 8 to 0; otherwise it rises by 8
/// to maxSpeed when more than 40 direction units away, else falls by 8 to 8. Returns 1 when not centred.
pub fn computeStepAndDisplacement(targetPos: ?*v.SVECTOR, outStep: ?*v.VECTOR, outDisplacement: ?*v.VECTOR, clampToZone: c_int, maxSpeed: c_int) callconv(.c) c_int {
    // pad is uninitialised in the original; RotTransPers loads it into the upper half of GTE register 1 (VZ0, which
    // MFC2 sign-extends from the low half, so nothing reads it). Zig's undefined would leave 0xAA there.
    var scaled: v.SVECTOR = .{ .vx = undefined, .vy = undefined, .vz = undefined, .pad = 0 };
    var fixed: v.VECTOR = undefined;
    var direction: v.VECTOR = undefined;
    var sxy: c_int = undefined;
    var p: c_int = undefined;
    var notCentered: c_int = undefined;
    fixed.vx = @as(c_int, targetPos.?.vx) *% 4096;
    fixed.vz = @as(c_int, targetPos.?.vz) *% 4096;
    fixed.vy = @as(c_int, targetPos.?.vy) *% 4096;
    if (clampToZone != 0) {
        c._clampPositionToZoneBounds(@ptrCast(&fixed), @ptrCast(&fixed));
    }
    scaled.vx = psx.conv(i16, @divTrunc(fixed.vx, 4096));
    scaled.vz = psx.conv(i16, @divTrunc(fixed.vz, 4096));
    scaled.vy = psx.conv(i16, @divTrunc(fixed.vy, 4096));
    const scratch = v.g.vs_scratch();
    c.SetRotMatrix(@ptrCast(&scratch.viewMatrix));
    c.SetTransMatrix(@ptrCast(&scratch.viewMatrix));
    _ = c.RotTransPers(@ptrCast(&scaled), &sxy, &p, &notCentered);
    if (clampToZone == 0) {
        const y = @divTrunc(sxy, 0x10000);
        if (psx.conv(c_uint, (sxy -% y *% 0x10000) -% 61) < 199) {
            if (y > 60 and y < 180) {
                notCentered = 0;
            } else {
                notCentered = 1;
            }
        } else {
            notCentered = 1;
        }
    } else {
        notCentered = 1;
    }
    const lookAt = &scratch.camera.lookAt;
    const out = outDisplacement.?;
    out.vx = @as(c_int, scaled.vx) *% 4096 -% lookAt.vx;
    fixed.vx = @divTrunc(out.vx, 4096);
    out.vz = @as(c_int, scaled.vz) *% 4096 -% lookAt.vz;
    fixed.vz = @divTrunc(out.vz, 4096);
    out.vy = @as(c_int, scaled.vy) *% 4096 -% lookAt.vy;
    fixed.vy = @divTrunc(out.vy, 4096);
    _ = c.VectorNormal(@ptrCast(&fixed), @ptrCast(&direction));
    const camera = v.g.vs_battle_cameraCurrentSpherical();
    if (notCentered != 0) {
        var distance: c_int = undefined;
        if (direction.vx != 0) {
            distance = @divTrunc(out.vx, direction.vx);
        } else if (direction.vz != 0) {
            distance = @divTrunc(out.vz, direction.vz);
        } else if (direction.vy != 0) {
            distance = @divTrunc(out.vy, direction.vy);
        } else {
            distance = 1;
        }
        if (distance > 40) {
            camera.speed +%= 8;
            if (maxSpeed < camera.speed) {
                camera.speed = maxSpeed;
            }
        } else {
            camera.speed -%= 8;
            if (camera.speed < 8) {
                camera.speed = 8;
            }
        }
    } else {
        camera.speed -%= 8;
        if (camera.speed < 0) {
            camera.speed = 0;
        }
    }
    outStep.?.vx = direction.vx *% camera.speed;
    outStep.?.vz = direction.vz *% camera.speed;
    outStep.?.vy = direction.vy *% camera.speed;
    return notCentered;
}
