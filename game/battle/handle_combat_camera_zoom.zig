//! The action's phase 4: the camera converges on the cast (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

fn camera() *v._sphericalCamera {
    return v.g.vs_battle_cameraCurrentSpherical();
}

/// The camera's delta mode is 1 or 3.
fn aiming() bool {
    return camera().delta.mode == 1 or camera().delta.mode == 3;
}

/// The camera's step toward its target (func_800780A8, _computeStepAndDisplacement at speed 0x28); the cast starts
/// (func_8007138C) once it no longer moves, does not turn and still aims.
fn castWhenSettled() void {
    var target: v.SVECTOR = undefined;
    var displacement: v.VECTOR = undefined;
    var step: v.VECTOR = undefined;
    c.func_800780A8(psx.cptr(c.SVECTOR, &target));
    _ = c._computeStepAndDisplacement(psx.cptr(c.SVECTOR, &target), psx.cptr(c.VECTOR, &step), psx.cptr(c.VECTOR, &displacement), 1, 0x28);
    if ((displacement.vx | displacement.vy | displacement.vz) == 0 and camera().delta.yaw == 0 and aiming()) {
        c.func_8007138C();
    }
}

/// orig: _handleCombatCameraZoom (src/BATTLE/BATTLE.PRG/146C.c)
/// While the camera aims (delta mode 1 or 3), by the zoom kind D_800F19CC->unk2C0E: 0 steps toward the target, 1
/// widens the projection to 0x300 and pulls the distance in to 0x600, 2 pushes the distance out to 0x900, each then
/// stepping (func_8007820C first); the cast starts once the camera has settled. Any other kind starts it as soon as
/// the camera does not turn.
pub fn handleCombatCameraZoom() callconv(.c) void {
    if (!aiming()) return;
    const zoom = v.g.D_800F19CC().*.?.unk2C0E;
    if (zoom == 0) {
        c.func_8007820C(1);
        castWhenSettled();
    } else if (zoom == 1) {
        c.func_8007820C(1);
        const projection = v.g.vs_main_projectionDistance();
        projection.* +%= 0x40;
        if (projection.* >= 0x300) {
            projection.* = 0x300;
        }
        c.SetGeomScreen(projection.*);
        if (camera().values.distance > 0x600) {
            camera().values.distance -%= 0xC0;
            if (camera().values.distance < 0x600) {
                camera().values.distance = 0x600;
            }
        }
        if (projection.* == 0x300) {
            castWhenSettled();
        }
    } else if (zoom == 2) {
        c.func_8007820C(1);
        if (camera().values.distance < 0x900) {
            camera().values.distance +%= 0xC0;
            if (camera().values.distance > 0x900) {
                camera().values.distance = 0x900;
            }
        }
        if (camera().values.distance == 0x900) {
            castWhenSettled();
        }
    } else if (camera().delta.yaw == 0 and aiming()) {
        c.func_8007138C();
    }
}
