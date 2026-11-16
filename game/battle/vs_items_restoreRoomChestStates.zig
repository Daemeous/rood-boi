//! Refreshes the room's section 13 entries from their state flags (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_items_restoreRoomChestStates (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets each room section 13 entry's flag from its state flag and marks its linked section 3 record.
pub fn vs_items_restoreRoomChestStates() callconv(.c) void {
    const room = v.g.vs_battle_roomData();
    if (psx.get(room.section13) != null) {
        const base = psx.get(room.section13).?;
        const section13Len: u32 = room.header.section13Len;
        const count: c_int = psx.conv(c_int, @as(usize, section13Len) / @sizeOf(v._mpdRoomSection13));
        var p: [*]v._mpdRoomSection13 = @ptrCast(base);
        var i: c_int = 0;
        while (i < count) : ({
            i +%= 1;
            p += 1;
        }) {
            const s3 = &p[0];
            const temp = c.vs_field_collisionCellAt(
                psx.conv(c_uint, s3.tileX),
                psx.conv(c_uint, s3.tileZ),
                psx.conv(c_int, s3.unk4),
            );
            if (c.vs_battle_getStateFlag(psx.conv(c_short, @as(c_int, s3.openedFlag) +% 0x3C0)) != 0) {
                s3.isOpen = 1;
                c.vs_field_finishRoomTiltRotation(psx.conv(c_int, s3.unk6));
                if (temp) |t| {
                    const sec3: *v._mpdRoomSection3 = @ptrCast(t);
                    psx.setBits(sec3, "unk0_10", 5);
                }
            } else {
                s3.isOpen = 0;
                if (temp) |t| {
                    const sec3: *v._mpdRoomSection3 = @ptrCast(t);
                    psx.setBits(sec3, "unk0_10", 0);
                }
            }
        }
    }
}
