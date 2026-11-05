//! Room-name table setup (D2: 2EA3C.s; oracle pc/src/room_geometry.c).
const c = @import("c_room_geometry");
const psx = @import("common").psx;

/// orig: vs_battle_initSceneAndGetRoomNames (pc/src/room_geometry.c)
/// Turns each 12-byte entry's size (word 1) into a pointer to its name and
/// returns the end of the names.
pub fn initSceneAndGetRoomNames(table: [*]c_uint) callconv(.c) [*]u8 {
    const count: c_uint = table[0];
    if (count == 0) c.pc_unimplemented("room-name table with no entries");
    const entries: [*]u8 = @ptrCast(table + 1);
    var name: [*]u8 = @ptrFromInt(@intFromPtr(entries) +% @as(usize, count *% 12));
    var i: c_uint = 0;
    while (i < count) : (i +%= 1) {
        const entry: [*]c_uint = @ptrCast(@alignCast(@as([*]u8, @ptrFromInt(@intFromPtr(entries) +% @as(usize, i *% 12)))));
        const size: c_uint = entry[1];
        entry[1] = psx.addr(name);
        name = @ptrFromInt(@intFromPtr(name) +% @as(usize, size));
    }
    return name;
}
