//! Installs a room's MPD section (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _loadMpdRoomSection (src/BATTLE/BATTLE.PRG/146C.c)
/// The section starts with the 24 part lengths (the room header); with any data, copies the header and the parts
/// into a heap block, points each part with a positive length at its bytes (null otherwise), marks the room loaded
/// (unk2 = `room`), fixes up the geometry, section E's two lists, the collision map's tile count and the scene's
/// rooms and door, then runs the room's setup calls. 0 for an empty section.
pub fn loadMpdRoomSection(room: c_int, data: ?*anyopaque) callconv(.c) c_int {
    const lengths: [*]c_int = @ptrCast(@alignCast(data.?));
    v.g.D_800F1BB6().* = 0;
    var size: c_uint = 0;
    var i: c_int = 0;
    while (i < 24) : (i += 1) { // i < 24
        size +%= psx.conv(c_uint, lengths[@intCast(i)]);
    }
    if (size == 0) {
        return 0;
    }
    const rooms = v.g.vs_battle_roomData();
    c.vs_main_memcpy(&rooms.header, data, 0x60);
    const heap = c.vs_main_allocHeap(size);
    c.vs_main_memcpy(heap, @ptrFromInt(@intFromPtr(data) + 0x60), size);
    size = 0;
    // The 24 part pointers from geometrySection on, set by their raw words as the C's psx_set_raw does.
    const parts: [*]u32 = @ptrCast(&rooms.geometrySection);
    i = 0;
    while (i < 24) : (i += 1) { // i < 24
        if (lengths[@intCast(i)] > 0) {
            parts[@intCast(i)] = @truncate(@intFromPtr(heap) +% size);
        } else {
            parts[@intCast(i)] = 0;
        }
        size +%= psx.conv(c_uint, lengths[@intCast(i)]);
    }
    rooms.unk0 = 1;
    rooms.unk1 = 0;
    rooms.unk2 = psx.conv(i16, room);
    if (psx.get(rooms.geometrySection) != null) {
        c.func_8008B390(11, psx.get(rooms.geometrySection));
    }
    if (psx.get(rooms.sectionE)) |section| {
        const after: *v._mpdRoomSectionE = psx.at(section, 1);
        psx.put(&section.unkC, @as(*i32, @ptrCast(after)));
        if (section.unk8 > 0) {
            psx.put(&section.unk10, @as(*i32, @ptrCast(after)));
            psx.put(&section.unk10, psx.at(@as([*]i32, @ptrCast(psx.get(section.unk10).?)), section.unk4 >> 2));
        } else {
            psx.put(&section.unk10, null);
        }
    }
    if (psx.get(rooms.collisionMapSection) != null) {
        psx.get(rooms.collisionMapSection).?.unk6 = psx.conv(i16, @as(c_int, psx.get(rooms.collisionMapSection).?.width) *%
            psx.get(rooms.collisionMapSection).?.height);
    }
    if (psx.get(rooms.sectionF) != null) {
        _ = c.vs_battle_initSceneAndGetRoomNames(psx.get(rooms.sectionF));
        c.vs_battle_setRoomsUnk0(@ptrCast(psx.get(rooms.sectionF)));
        psx.at(&psx.get(rooms.sectionF).?.rooms, 0).visited = 1;
        const flags = v.g.vs_main_stateFlags();
        if (flags.doorEntered == 0) {
            c.vs_battle_setDoorEntered2(v.g.vs_battle_doorEntered().*);
        } else {
            c.vs_battle_setDoorEntered2(@as(c_int, flags.doorEntered) - 1);
            flags.doorEntered = 0;
        }
    }
    c.func_8008B6B4();
    c.func_80092548();
    c.func_8008D710();
    c.func_8008C538();
    c.func_80093AB4();
    c.func_8008E8F8();
    c.func_8008E938();
    c.func_8008B320();
    psx.global(u8, "D_800F1BBC").* = 0;
    if (psx.get(rooms.sectionE) != null) {
        c.vs_main_wait();
    }
    return 1;
}
