//! Room locks (map section A: doors with a key or sigil) (src/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8008D5FC (src/BATTLE/BATTLE.PRG/146C.c)
/// Operates an idle lock: a locked one (state 0) is queued as event 2 with a 20-tick delay, an open one toggles now.
pub fn operateLock(lock: ?*v._mpdRoomSectionA) callconv(.c) void {
    if (psx.conv(i8, lock.?.unk0.unkB) == 0) {
        lock.?.unk0.unkB = 1;
        if (lock.?.unk0.unk8.s16 == 0) {
            lock.?.unk0.unkA = 0x14;
            c.func_8008C070(2, psx.cptr(c._mpdRoomSectionA, lock.?));
            return;
        }
        c.func_8008D658(psx.cptr(c._mpdRoomSectionA, lock.?));
    }
}
