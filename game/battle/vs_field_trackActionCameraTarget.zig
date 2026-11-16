//! Camera target update for the battle camera modes (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_trackActionCameraTarget (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Picks the camera's target position for the current camera mode and steps the camera toward it.
pub fn vs_field_trackActionCameraTarget(arg0: c_int) callconv(.c) void {
    var sp10: v.func_8006EBF8_t = undefined;
    var targetPos: v.SVECTOR = undefined;

    _ = c.vs_actors_actorPlacement(arg0, &sp10);
    targetPos.vx = sp10.location.position.vx;
    targetPos.vz = sp10.location.position.vz;
    targetPos.vy = psx.conv(i16, @as(c_int, sp10.location.position.vy) -% 0x5A);
    c.vs_field_updateCameraTileConstraint(arg0, @ptrCast(&sp10.location.packedTile.fields));

    switch (v.g._cameraMode().*) {
        1, 3 => {
            c.vs_field_updateCameraOrbitInput();
            const ability = v.g.vs_battle_actionState().*.?;
            if (ability.unk298D != 0xFF and ability.previewCount != 0) {
                // unk298D is a u8 index that the C does not bound to the 23 entries.
                const entry = psx.at(&ability.previews, ability.unk298D);
                targetPos.vx = entry.unk0.vx;
                targetPos.vy = entry.unk0.vy;
                targetPos.vz = entry.unk0.vz;
            }
            _ = c._stepTowardTarget(psx.cptr(c.SVECTOR, &targetPos), 0, 0x28);
        },
        2 => {
            c.vs_field_updateCameraOrbitInput();
            const ability = v.g.vs_battle_actionState().*.?;
            targetPos.vx = ability.unk2998.vx;
            targetPos.vy = ability.unk2998.vy;
            targetPos.vz = ability.unk2998.vz;
            if (c.func_800C4734() == 1) {
                _ = c._stepTowardTarget(psx.cptr(c.SVECTOR, &targetPos), 0, 8);
                return;
            }
            _ = c._stepTowardTarget(psx.cptr(c.SVECTOR, &targetPos), 0, 0x28);
        },
        11 => {
            c.vs_field_updateCameraOrbitInput();
            _ = c._stepTowardTarget(psx.cptr(c.SVECTOR, &targetPos), 1, 0x28);
        },
        4 => {
            c.vs_field_updateCameraOrbitInput();
        },
        else => {},
    }
}
