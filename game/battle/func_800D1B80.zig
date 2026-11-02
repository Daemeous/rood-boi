//! Applies the effect camera (src/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D1B80 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Sets the game camera from an effect camera: its position and look-at moved by the unk20/24/28 offsets along its yaw and pitch, roll, clips and projection.
pub fn func_800D1B80(camera_arg: ?*v.D_800F54D8_t) callconv(.c) void {
    const camera = camera_arg.?;
    var offset: v.VECTOR = undefined;
    var position: v.VECTOR = undefined;
    const dx = (camera.position.vx -% camera.lookAt.vx) >> 12;
    const dz = (camera.position.vz -% camera.lookAt.vz) >> 12;
    const yaw = c.ratan2(dx, dz);
    offset.vx = c.rcos(yaw) *% (camera.unk20 >> 12);
    offset.vz = -%c.rsin(yaw) *% (camera.unk20 >> 12);
    const pitch = c.ratan2((camera.position.vy -% camera.lookAt.vy) >> 12, c.vs_gte_rsqrt(dx *% dx +% dz *% dz));
    offset.vy = -%c.rsin(pitch) *% (camera.unk28 >> 12);
    var horizontal = -%c.rcos(pitch) *% (camera.unk28 >> 12);
    offset.vy +%= c.rcos(pitch) *% (camera.unk24 >> 12);
    horizontal +%= -%c.rsin(pitch) *% (camera.unk24 >> 12);
    horizontal >>= 12;
    offset.vx +%= c.rsin(yaw) *% horizontal;
    offset.vz +%= c.rcos(yaw) *% horizontal;
    position.vx = camera.position.vx +% offset.vx;
    position.vy = camera.position.vy +% offset.vy;
    position.vz = camera.position.vz +% offset.vz;
    c.vs_battle_setCameraPosition(@ptrCast(&position));
    position.vx = camera.lookAt.vx +% offset.vx;
    position.vy = camera.lookAt.vy +% offset.vy;
    position.vz = camera.lookAt.vz +% offset.vz;
    c.vs_battle_setCameraLookAt(@ptrCast(&position));
    const roll_offset = camera.unk2C -% 0x8000;
    c.vs_battle_setCameraRoll(camera.roll +% roll_offset);
    c.vs_battle_setNearClip(camera.nearClip);
    c.vs_battle_setProjectionDistance(camera.projectionDistance);
    // The C calls it through a cast with one argument: the second is whatever the register holds.
    const SetFarClip = *const fn (c_int) callconv(.c) void;
    @as(SetFarClip, @ptrCast(&c.vs_battle_setFarClip))(camera.farClip);
    c.func_8007ACB0();
}
