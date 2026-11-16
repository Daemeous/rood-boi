//! Room section 12 entry lookup (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_field_movingPlatformTargetWaypoint (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the address of section 12 entry `arg0` plus its 8-byte header and its 16-byte-scaled extent, or null when the room has no section 12.
pub fn vs_field_movingPlatformTargetWaypoint(arg0: c_int) callconv(.c) ?*anyopaque {
    if (psx.get(v.g.vs_battle_roomData().section12)) |section12| {
        const temp_v1 = psx.at(section12, arg0);
        const off: c_int = 8 +% @as(c_int, psx.getBits(temp_v1, "unk0_27")) *% 16;
        return @ptrCast(psx.byteAt(u8, temp_v1, @as(isize, off)));
    }
    return null;
}
