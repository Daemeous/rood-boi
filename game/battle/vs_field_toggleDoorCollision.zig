//! Flips the flag bits of the room-section cell under a section and of its neighbour in the direction the section selects (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_toggleDoorCollision (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Toggles the flag bits of the cell under a room section and, when unk6 >= 4, of the neighbour that unk6 - 4 selects.
pub fn vs_field_toggleDoorCollision(arg0: ?*v._mpdRoomSectionA) callconv(.c) void {
    const sec = arg0.?;

    var var_a0: ?*v._mpdRoomSection3 = @ptrCast(c.vs_field_collisionCellAt(
        psx.conv(c_uint, sec.unk0.unk0),
        psx.conv(c_uint, sec.unk0.unk2),
        psx.conv(c_int, sec.unk0.unk4),
    ));
    if (var_a0) |s| {
        psx.setBits(s, "unk0_10", psx.getBits(s, "unk0_10") ^ 5);
    }

    var_a0 = null;
    switch (@as(c_int, sec.unk0.unk6) -% 4) {
        0 => var_a0 = @ptrCast(c.vs_field_collisionCellAt(
            psx.conv(c_uint, sec.unk0.unk0),
            psx.conv(c_uint, @as(c_int, sec.unk0.unk2) -% 1),
            psx.conv(c_int, sec.unk0.unk4),
        )),
        1 => var_a0 = @ptrCast(c.vs_field_collisionCellAt(
            psx.conv(c_uint, @as(c_int, sec.unk0.unk0) +% 1),
            psx.conv(c_uint, sec.unk0.unk2),
            psx.conv(c_int, sec.unk0.unk4),
        )),
        2 => var_a0 = @ptrCast(c.vs_field_collisionCellAt(
            psx.conv(c_uint, sec.unk0.unk0),
            psx.conv(c_uint, @as(c_int, sec.unk0.unk2) +% 1),
            psx.conv(c_int, sec.unk0.unk4),
        )),
        3 => var_a0 = @ptrCast(c.vs_field_collisionCellAt(
            psx.conv(c_uint, @as(c_int, sec.unk0.unk0) -% 1),
            psx.conv(c_uint, sec.unk0.unk2),
            psx.conv(c_int, sec.unk0.unk4),
        )),
        else => {},
    }

    if (sec.unk0.unk6 >= 4) {
        if (var_a0) |s| {
            psx.setBits(s, "unk0_10", psx.getBits(s, "unk0_10") ^ 5);
        }
    }
}
