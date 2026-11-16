//! Records the door section being entered, or forgets it (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_setEntranceDoorRecord (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Copies the 12-byte door entry to vs_field_entranceDoorRecord and records its door id (from its byte 8) in vs_battle_doorEntered;
/// with no entry, clears vs_field_entranceDoorRecord and records -1.
pub fn vs_field_setEntranceDoorRecord(door: ?*[12]u8) callconv(.c) void {
    if (door) |entry| {
        c.vs_main_memcpy(v.g.vs_field_entranceDoorRecord(), entry, @sizeOf(v._mpdRoomDoorSection_t));
        const id = c._getDoorId(entry[8]);
        v.g.vs_battle_doorEntered().* = psx.conv(i16, id);
    } else {
        c.vs_main_bzero(v.g.vs_field_entranceDoorRecord(), @sizeOf(v._mpdRoomDoorSection_t));
        v.g.vs_battle_doorEntered().* = -1;
    }
}
