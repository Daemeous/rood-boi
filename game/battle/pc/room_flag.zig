//! Room record flag bits (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: room_flag (pc/src/battle_movement.c)
/// Clears or sets bit `bit` of the room record's byte 0xE. Bit 31 inverts the
/// sign test, as in the original (a clear of bit 31 sets the byte, a set
/// clears it).
fn roomFlag(bit: c_int, clear: bool) void {
    const address = v.g.D_800F58BC().*;
    if (address == 0) return;
    const room: [*]u8 = @ptrFromInt(address);
    var mask: c_int = @as(c_int, 1) << @intCast(bit & 31);
    if (clear) mask = -1 -% mask;
    const byte: c_int = room[0xe];
    room[0xe] = psx.conv(u8, if (mask < 0) byte & mask else byte | mask);
}

/// orig: func_800E4BD8 (pc/src/battle_movement.c)
/// Clears room flag `bit`.
pub fn clearRoomFlag(bit: c_int) callconv(.c) void {
    roomFlag(bit, true);
}

/// orig: func_800E4BE0 (pc/src/battle_movement.c)
/// Sets room flag `bit`.
pub fn setRoomFlag(bit: c_int) callconv(.c) void {
    roomFlag(bit, false);
}
