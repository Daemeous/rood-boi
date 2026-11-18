//! Room section 9 lookup for the battle room (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// Converts a guest address to the pointer the C returns, without ReleaseSafe's alignment check: the C returns the stored address unchanged.
inline fn guestPtr(raw: usize) ?*v._mpdRoomSection9 {
    @setRuntimeSafety(false);
    return @ptrFromInt(raw);
}

/// orig: func_8008E400 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the battle room's section 9 and stores its entry count (section9Len / 12) in `arg0`, or 0 and null.
pub fn func_8008E400(arg0: [*c]c_int) callconv(.c) ?*v._mpdRoomSection9 {
    const flags = v.g.vs_main_stateFlags();
    if (flags.unk20 != 0 or flags.unk40 != 0) {
        arg0.?.* = 0;
        return null;
    }
    const room = v.g.vs_battle_roomData();
    const section9_raw: usize = @intFromPtr(psx.get(room.section9));
    if (section9_raw != 0) {
        arg0.?.* = psx.conv(c_int, room.header.section9Len / 12);
        return guestPtr(section9_raw);
    }
    arg0.?.* = 0;
    return null;
}
