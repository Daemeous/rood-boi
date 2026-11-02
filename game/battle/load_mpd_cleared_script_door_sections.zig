//! The room's MPD cleared, script and door sections (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: _loadMpdClearedScriptDoorSections (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Clears flag 0xA9 (and the room flags 32..63 on a fresh start), copies the cleared and door
/// sections into heap blocks and the script section into D_800F4C30; returns 1 (copying no script)
/// when D_800F4BE2 was 1, which it moves to 2.
pub fn loadMpdClearedScriptDoorSections(cleared_section: ?*anyopaque, cleared_section_len: u16, script_section: ?*anyopaque,
    script_section_len: u16, door_section: ?*anyopaque, door_section_len: u16, start_state: c_int) callconv(.c) c_int {
    c.vs_battle_setStateFlag(0xA9, 0);
    if (start_state == 0) {
        var i: i16 = 32;
        while (i < 64) : (i += 1) c.vs_battle_setStateFlag(i, 0); // i < 64: no overflow
    }
    if (cleared_section_len != 0) {
        v.g._mpdClearedSection().* = c.vs_main_allocHeap(cleared_section_len);
        _ = c.memcpy(v.g._mpdClearedSection().*, cleared_section, @as(c_int, cleared_section_len));
    } else {
        v.g._mpdClearedSection().* = null;
    }
    if (door_section_len != 0) {
        v.g._mpdRoomDoorSection().* = c.vs_main_allocHeap(door_section_len);
        _ = c.memcpy(v.g._mpdRoomDoorSection().*, door_section, @as(c_int, door_section_len));
    } else {
        v.g._mpdRoomDoorSection().* = null;
    }
    if (v.g.D_800F4BE2().* == 1) {
        v.g.D_800F4BE2().* = 2;
        return 1;
    }
    if (v.g.D_800F4BE2().* == 3) {
        v.g.D_800F4C34().* = 0;
        v.g.D_800F4BE2().* = 0;
        v.g.D_800F4BB8().* = 0;
    }
    v.g._mpdScriptSection().* = @intFromBool(script_section_len != 0);
    if (v.g._mpdScriptSection().* != 0) {
        _ = c.memcpy(v.g.D_800F4C30().*, script_section, @as(c_int, script_section_len));
    }
    return 0;
}
