//! Looks up an entry in the room's geometry section (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");

/// orig: vs_field_findRoomGeometryGroup (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Returns the address of geometry entry `index` (16 words each, after its count word), or null when there is none.
pub fn vs_field_findRoomGeometryGroup(arg0: c_int) callconv(.c) ?*anyopaque {
    const section = psx.get(v.g.vs_battle_roomData().geometrySection) orelse return null;
    const count: c_int = section.*;
    // a1 = section + 1 (int*): 4 bytes on; a1 + arg0 * 16 ints: arg0 * 64 bytes on.
    if (arg0 >= 0 and arg0 < count) {
        return @ptrFromInt(@intFromPtr(section) +% @as(usize, @intCast(arg0)) *% 64 +% 4);
    }
    return null;
}
