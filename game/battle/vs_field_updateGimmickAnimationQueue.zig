//! Walks the eight room-object slots and retires the ones whose timers have run out (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_updateGimmickAnimationQueue (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Ticks each active room-object slot: finishes state 1 when its callback reports done, and retires states 2 and 3 when their counters reach zero.
pub fn vs_field_updateGimmickAnimationQueue() callconv(.c) void {
    if (v.g.vs_field_pendingGimmickAnimationCount().* <= 0) {
        return;
    }

    const slots = v.g.vs_field_gimmickAnimationQueue();
    for (0..8) |i| {
        const slot = &slots[i];
        if (slot.unk0 == 1) {
            if (c.vs_field_finishDoorAnimation(@ptrCast(psx.get(slot.unk4))) != 0) {
                slot.unk0 = 0;
                psx.put(&slot.unk4, null);
                v.g.vs_field_pendingGimmickAnimationCount().* -%= 1;
            }
        } else if (slot.unk0 == 2) {
            const entry = psx.get(slot.unk4).?;
            // unkA is a u8 promoted to int before the shift: the shift wraps in C, so it wraps here.
            entry.trigger.unkA -%= 1;
            const shifted: c_int = @bitCast(@as(c_uint, entry.trigger.unkA) << 24);
            if (shifted <= 0) {
                c.vs_field_toggleRoomSwitch(@ptrCast(entry));
                slot.unk0 = 0;
                psx.put(&slot.unk4, null);
                v.g.vs_field_pendingGimmickAnimationCount().* -%= 1;
            }
        } else if (slot.unk0 == 3) {
            const entry = psx.get(slot.unk4).?;
            entry.delayFrames -%= 1;
            if (entry.delayFrames == 0) {
                _ = c.vs_field_startDoorAnimation(@ptrCast(entry));
                slot.unk0 = 0;
                psx.put(&slot.unk4, null);
                v.g.vs_field_pendingGimmickAnimationCount().* -%= 1;
            }
        }
    }
}
