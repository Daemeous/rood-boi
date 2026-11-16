//! Battle-field camera check for the unit at arg0, then the camera's follow or release call (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_updateCameraTileConstraint (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls func_8008C2C0 with the camera's slot and distance when the room section's flag is set, else vs_field_clearGeometryOcclusion.
pub fn vs_field_updateCameraTileConstraint(arg0: c_int, arg1: ?*v.func_8006EBF8_t_fields) callconv(.c) void {
    const temp_v0: c_uint = psx.conv(c_uint, c.vs_actors_actorStateFlags(arg0)) & 0x70000;
    var var_a0: u8 = 0;
    var var_a1: u8 = 0;
    var var_a2: c_int = 0;
    if (temp_v0 != 0) {
        var_a2 = psx.conv(c_int, temp_v0 >> 16);
    } else {
        const f = arg1.?;
        var_a0 = psx.getBits(f, "unk0_0");
        var_a1 = psx.getBits(f, "unk0_16");
        var_a2 = @as(c_int, psx.getBits(f, "unk0_8"));
    }

    // vs_field_collisionCellAt returns a room section whose unk0 word has bit 18 tested (byte 2, bit 2).
    const room = c.vs_field_collisionCellAt(psx.conv(c_uint, var_a0), psx.conv(c_uint, var_a1), var_a2);
    if (((psx.byteAt(u8, room.?, 2).* >> 2) & 1) != 0) {
        const f = arg1.?;
        _ = c.func_8008C2C0(
            @as(c_int, psx.getBits(f, "unk0_0")),
            @as(c_int, psx.getBits(f, "unk0_16")),
            @as(c_int, psx.getBits(f, "unk0_8")),
            @intFromBool(v.g.vs_battle_cameraCurrentSpherical().initialDistance == 0x900),
        );
        return;
    }
    c.vs_field_clearGeometryOcclusion();
}
