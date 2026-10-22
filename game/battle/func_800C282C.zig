//! Writes the camera yaw's cos and sin to the scratchpad as a 2x2 rotation and stores the yaw in D_800EB9B8 (src/BATTLE/BATTLE.PRG/58578.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_58578");
const c = @import("c_58578");

/// orig: func_800C282C (src/BATTLE/BATTLE.PRG/58578.c)
/// Syncs the camera angles, writes the yaw's rotation matrix terms to the scratchpad, stores the yaw in D_800EB9B8 and returns the scratchpad block.
pub fn func_800C282C() callconv(.c) ?*anyopaque {
    var camera: v._sphericalCamera = undefined;
    const p: [*]i16 = @ptrFromInt(0x1F800350);
    _ = c.vs_battle_syncCameraAnglesFromPosition(psx.cptr(c._sphericalCamera, &camera));
    const yaw: c_int = camera.values.yaw;
    const cosYaw: c_int = c.rcos(yaw);
    const sinYaw: c_int = c.rsin(yaw);
    p[0] = psx.conv(i16, cosYaw);
    p[2] = psx.conv(i16, sinYaw);
    p[8] = psx.conv(i16, -%sinYaw);
    p[10] = psx.conv(i16, cosYaw);
    v.g.D_800EB9B8().*.?.unk22 = psx.conv(i16, yaw);
    return @ptrFromInt(0x1F800350);
}
