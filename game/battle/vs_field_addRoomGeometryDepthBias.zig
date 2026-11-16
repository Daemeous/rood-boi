//! Adds an amount to a halfword of the record that vs_field_findRoomGeometryGroup finds
//! (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_2842C");

/// orig: vs_field_addRoomGeometryDepthBias (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Adds `amount` to the halfword at offset 2 of record `index`; returns 1 if the record exists, else 0.
pub fn vs_field_addRoomGeometryDepthBias(arg0: c_int, arg1: c_int) callconv(.c) c_int {
    const found: ?*anyopaque = c.vs_field_findRoomGeometryGroup(arg0);
    const temp_v0: ?[*]align(1) u16 = @ptrCast(found);
    if (temp_v0) |record| {
        record[1] = psx.conv(u16, @as(c_int, record[1]) +% arg1);
        return 1;
    }
    return 0;
}
