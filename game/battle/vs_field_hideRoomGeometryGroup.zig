//! Sets flag 0x100 on the record vs_field_findRoomGeometryGroup finds for an index (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const c = @import("c_2842C");

/// orig: vs_field_hideRoomGeometryGroup (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Looks up the record for `arg0` and sets its 0x100 flag; returns 1 if the record exists, else 0.
pub fn vs_field_hideRoomGeometryGroup(arg0: c_int) callconv(.c) c_int {
    const temp_v0 = c.vs_field_findRoomGeometryGroup(arg0);
    if (temp_v0 == null) return 0;
    const flags: *align(1) c_int = @ptrCast(temp_v0.?);
    flags.* |= 0x100;
    return 1;
}
