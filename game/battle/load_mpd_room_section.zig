//! Installs a room's MPD section (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _loadMpdRoomSection (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// The section starts with the 24 part lengths (the room header); with any data, copies the header and the parts
/// into a heap block, points each part with a positive length at its bytes (null otherwise), marks the room loaded
/// (unk2 = `room`), fixes up the geometry, section E's two lists, the collision map's tile count and the scene's
/// rooms and door, then runs the room's setup calls. 0 for an empty section.
pub fn loadMpdRoomSection(room: c_int, data: ?*anyopaque) callconv(.c) c_int {
    const lengths: [*]c_int = @ptrCast(@alignCast(data.?));
    v.g.vs_field_isRoomTintActive().* = 0;
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
    // The 24 part pointers from geometrySection on.
    const parts: [*]?*anyopaque = @ptrCast(@alignCast(&rooms.geometrySection));
    i = 0;
    while (i < 24) : (i += 1) { // i < 24
        if (lengths[@intCast(i)] > 0) {
            parts[@intCast(i)] = @ptrFromInt(@intFromPtr(heap) +% size);
        } else {
            parts[@intCast(i)] = null;
        }
        size +%= psx.conv(c_uint, lengths[@intCast(i)]);
    }
    rooms.unk0 = 1;
    rooms.unk1 = 0;
    rooms.unk2 = psx.conv(i16, room);
    if (psx.get(rooms.geometrySection) != null) {
        c.vs_field_offsetRoomTextureIds(11, psx.get(rooms.geometrySection));
    }
    if (psx.get(rooms.sectionE)) |section| {
        // Offsets from the section's start, not addresses: the section is file data with 4-byte slots.
        section.unkC = @sizeOf(v._mpdRoomSectionE);
        if (section.unk8 > 0) {
            section.unk10 = @sizeOf(v._mpdRoomSectionE) +% @as(u32, @bitCast(section.unk4 >> 2)) *% 4;
        } else {
            section.unk10 = 0;
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
    c.vs_field_resetGimmickAnimationQueue();
    c.vs_field_initializeRoomEffects();
    c.vs_items_restoreRoomChestStates();
    c.vs_field_initializeMovingPlatforms();
    c.vs_battle_resetFloatingLabels();
    c.func_8008E8F8();
    c.vs_field_initializeRoomBackgroundColors();
    c.vs_field_uploadRoomPalettes();
    psx.Global(u8, "D_800F1BBC").ptr().* = 0;
    if (psx.get(rooms.sectionE) != null) {
        c.vs_main_wait();
    }
    return 1;
}
