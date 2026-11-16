//! Toggles a room section's state, clears its timer byte and sets the matching
//! section-3 flag (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_toggleRoomSwitch (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Plays the toggle sounds for a room section, flips its state and sets the flag of the section it finds.
pub fn vs_field_toggleRoomSwitch(section: ?*v._mpdRoomSectionA) callconv(.c) void {
    const room = section.?;

    if (room.trigger.unk8.s16 == 0) {
        _ = c.vs_main_playSfxDefault(0x7E, 0x3B);
        _ = c.vs_main_playSfxDefault(0x7E, 0x3C);
    } else {
        _ = c.vs_main_playSfxDefault(0x7E, 0x3D);
        _ = c.vs_main_playSfxDefault(0x7E, 0x3E);
    }
    _ = c.vs_field_startRoomEffect(@as(c_int, room.trigger.unk6));
    room.trigger.unkB = 0;
    room.trigger.unk8.s16 = @as(i16, @intFromBool(room.trigger.unk8.s16 == 0));
    const temp: ?*v._mpdRoomSection3 = @ptrCast(c.vs_field_collisionCellAt(
        psx.conv(c_uint, room.trigger.tileX),
        psx.conv(c_uint, room.trigger.tileZ),
        psx.conv(c_int, room.trigger.floorLayer),
    ));
    if (temp) |t| {
        psx.setBits(t, "unk0_10", psx.conv(u3, @as(c_int, room.trigger.unk8.s16) *% 5));
    }
}
