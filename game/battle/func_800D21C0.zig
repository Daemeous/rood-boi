//! The camera between its two keyframes, linearly (src/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D21C0 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// D_800F54D8's position, look-at (in whole units, back in 4.12), roll, far and near clip from keyframes 0 and 1 at frame D_800F54D0 of D_800F55A0, linearly.
pub fn func_800D21C0() callconv(.c) void {
    const keys = v.g.D_800F5520();
    const camera = v.g.D_800F54D8();
    var start: v.VECTOR = undefined;
    var end: v.VECTOR = undefined;
    var result: v.VECTOR = undefined;
    start.vx = keys[0].position.vx >> 12;
    start.vy = keys[0].position.vy >> 12;
    start.vz = keys[0].position.vz >> 12;
    end.vx = keys[1].position.vx >> 12;
    end.vy = keys[1].position.vy >> 12;
    end.vz = keys[1].position.vz >> 12;
    c.func_800D1884(@ptrCast(&start), @ptrCast(&end), v.g.D_800F55A0().*, v.g.D_800F54D0().*, @ptrCast(&result));
    camera.position.vx = result.vx << 12;
    camera.position.vy = result.vy << 12;
    camera.position.vz = result.vz << 12;
    start.vx = keys[0].lookAt.vx >> 12;
    start.vy = keys[0].lookAt.vy >> 12;
    start.vz = keys[0].lookAt.vz >> 12;
    end.vx = keys[1].lookAt.vx >> 12;
    end.vy = keys[1].lookAt.vy >> 12;
    end.vz = keys[1].lookAt.vz >> 12;
    c.func_800D1884(@ptrCast(&start), @ptrCast(&end), v.g.D_800F55A0().*, v.g.D_800F54D0().*, @ptrCast(&result));
    camera.lookAt.vx = result.vx << 12;
    camera.lookAt.vy = result.vy << 12;
    camera.lookAt.vz = result.vz << 12;
    camera.roll = c.func_800D1904(keys[0].roll, keys[1].roll, v.g.D_800F55A0().*, v.g.D_800F54D0().*);
    camera.farClip = c.func_800D1904(keys[0].farClip, keys[1].farClip, v.g.D_800F55A0().*, v.g.D_800F54D0().*);
    camera.nearClip = c.func_800D1904(keys[0].nearClip, keys[1].nearClip, v.g.D_800F55A0().*, v.g.D_800F54D0().*);
}
