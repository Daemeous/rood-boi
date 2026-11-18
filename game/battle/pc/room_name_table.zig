//! Room-name table setup (D2: 2EA3C.s; oracle pc/src/room_geometry.c).
const c = @import("c_room_geometry");
const arm_file = @import("../arm_file.zig");

/// orig: vs_battle_initSceneAndGetRoomNames (pc/src/room_geometry.c)
/// Turns each 12-byte entry's size (word 1) into the offset of its blob (arm_file.zig) and returns the end of the blobs, where the names are.
pub fn initSceneAndGetRoomNames(table: [*]c_uint) callconv(.c) [*]u8 {
    const count: c_uint = table[0];
    if (count == 0) c.pc_unimplemented("room-name table with no entries");
    return arm_file.initNames(@ptrCast(table), count);
}
