//! Battle room-section lock and switch handling, dispatched on the trigger's
//! kind bits in unk6 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_applyUnlockEffectToRoomObject (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Opens a lock or switch slot when its state is in range and the check passes; otherwise runs the fallback.
pub fn vs_field_applyUnlockEffectToRoomObject(arg0: ?*v.func_8006CE70_t) callconv(.c) void {
    const self = arg0.?;

    switch (@as(c_int, self.unk6) & 0x1F0) {
        0x10 => {
            var sp10: c_int = undefined;
            const rooms: [*c]v._mpdRoomSectionA = @ptrFromInt(@intFromPtr(c.func_8008E3B8(&sp10)));
            const slot: usize = @intCast(@as(c_int, self.unk6) & 0xF);
            const entry = &rooms[slot];
            const state: c_int = entry.trigger.unk8.lock.state;

            if (state < 4 and state >= 2 and c.vs_field_rollRiskLockBreak() != 0) {
                entry.trigger.unk8.lock.state = 0;

                c.vs_main_playSfxDefault(0x7E, 0x3F);
                c.vs_main_playSfxDefault(0x7E, 0x40);
            } else {
                c.vs_battle_queueWorldLockedLabel(self);
            }
        },
        0x20, 0x100 => {
            var sp10: c_int = undefined;
            const rooms: [*c]v._mpdRoomSection13 = @ptrFromInt(@intFromPtr(c.vs_items_roomTreasureChests(&sp10)));
            const slot: usize = @intCast(@as(c_int, self.unk6) & 0xF);
            const entry = &rooms[slot];

            if (entry.unkC < 4 and entry.unkC >= 2 and
                c.vs_battle_getStateFlag(psx.conv(c_short, @as(c_int, entry.openedFlag) +% 0x3C0)) == 0 and
                c.vs_field_rollRiskLockBreak() != 0)
            {
                entry.unkC = 0;

                c.vs_main_playSfxDefault(0x7E, 0x3F);
                c.vs_main_playSfxDefault(0x7E, 0x40);
            } else {
                c.vs_battle_queueWorldLockedLabel(self);
            }
        },
        else => {},
    }
}
