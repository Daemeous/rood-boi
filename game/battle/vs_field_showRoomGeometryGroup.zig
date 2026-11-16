//! Clears bit 0x100 of the word `vs_field_findRoomGeometryGroup` finds for an index
//! (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const c = @import("c_2842C");

/// orig: vs_field_showRoomGeometryGroup (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Clears bit 0x100 of the word `vs_field_findRoomGeometryGroup(arg0)` returns; 1 if there was one, else 0.
pub fn vs_field_showRoomGeometryGroup(arg0: c_int) callconv(.c) c_int {
    const temp_v0: ?*align(1) c_int = @ptrCast(c.vs_field_findRoomGeometryGroup(arg0));
    if (temp_v0) |word| {
        word.* &= ~@as(c_int, 0x100);
        return 1;
    }
    return 0;
}
