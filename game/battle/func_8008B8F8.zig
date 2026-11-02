//! Records the door section being entered, or forgets it (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8008B8F8 (src/BATTLE/BATTLE.PRG/146C.c)
/// Copies the 12-byte door entry to D_800F1CC8 and records its door id (from its byte 8) in vs_battle_doorEntered;
/// with no entry, clears D_800F1CC8 and records -1.
pub fn func_8008B8F8(door: ?*[12]u8) callconv(.c) void {
    if (door) |entry| {
        c.vs_main_memcpy(v.g.D_800F1CC8(), entry, @sizeOf(v._mpdRoomDoorSection_t));
        const id = c._getDoorId(entry[8]);
        v.g.vs_battle_doorEntered().* = psx.conv(i16, id);
    } else {
        c.vs_main_bzero(v.g.D_800F1CC8(), @sizeOf(v._mpdRoomDoorSection_t));
        v.g.vs_battle_doorEntered().* = -1;
    }
}
