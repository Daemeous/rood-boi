//! Battle frame camera setup: room and camera capture, view matrix, and the camera's scratchpad pointers (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_drawRoomGeometry (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Captures the room and camera for lighting, loads the view matrix from the scratchpad camera, and runs the camera passes that follow it.
pub fn vs_field_drawRoomGeometry(arg0: ?*anyopaque) callconv(.c) void {
    // ROOD_PC: lighting capture of world-space room geometry and this frame's camera.
    c.pc_scene_room(arg0);
    const scratch: *anyopaque = common.view.at(anyopaque, common.view.matrix_offset);
    c.pc_scene_camera(scratch);

    const camera = common.view.at(v.MATRIX, common.view.matrix_offset);
    common.gte.setFrame(camera); // the room's group matrices and packets read the frame's view
    c.vs_render_updateRoomGroupTransforms(arg0);

    v.g.D_800F1CC0().* = v.g.vs_scratch().unk0;

    c.vs_render_drawRoomGeometry(arg0, v.g.D_800F19C8().*);

    v.g.D_800F1BB8().* = v.g.vs_scratch().unk0;

    if (v.g.D_800F1BB0().unk6 != 0) {
        c.vs_render_tintRoomGeometryPrimitives(v.g.D_800F1CC0().*, v.g.D_800F1BB8().*, psx.cptr(c.vs_field_RoomTint, v.g.D_800F1BB0()), 0);
    }
}
