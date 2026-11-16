//! Room record flag bits (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: room_flag (pc/src/battle_movement.c)
/// Clears or sets bit `bit` of the room record's byte 0xE. Bit 31 inverts the
/// sign test, as in the original (a clear of bit 31 sets the byte, a set
/// clears it).
fn roomFlag(bit: c_int, clear: bool) void {
    const room = v.g.vs_battle_aiRoomAiState().* orelse return;
    var mask: c_int = @as(c_int, 1) << @intCast(bit & 31);
    if (clear) mask = -1 -% mask;
    const byte: c_int = room.closedGates;
    room.closedGates = psx.conv(u8, if (mask < 0) byte & mask else byte | mask);
}

/// orig: vs_battle_aiClearAiContactOccupancy (pc/src/battle_movement.c)
/// Clears room flag `bit`.
pub fn clearRoomFlag(bit: c_int) callconv(.c) void {
    roomFlag(bit, true);
}

/// orig: vs_battle_aiSetAiContactOccupancy (pc/src/battle_movement.c)
/// Sets room flag `bit`.
pub fn setRoomFlag(bit: c_int) callconv(.c) void {
    roomFlag(bit, false);
}
