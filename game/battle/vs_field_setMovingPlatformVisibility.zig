//! Sets a room section 12 entry's state and mirrors it into the word its id selects (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_setMovingPlatformVisibility (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores `arg1 - 1` as entry `arg0`'s state and sets or clears bit 0x100 of the word its id finds.
pub fn vs_field_setMovingPlatformVisibility(arg0: c_int, arg1: c_int) callconv(.c) void {
    if (psx.get(v.g.vs_battle_roomData().section12)) |section12| {
        const entry = psx.at(section12, arg0);
        psx.setBits(entry, "unk0_9", psx.conv(u1, arg1 -% 1));
        const word = c.vs_field_findRoomGeometryGroup(@as(c_int, psx.getBits(entry, "unk0_0")));
        if (word != null) {
            if (psx.getBits(entry, "unk0_9") == 0) {
                word.* &= ~@as(c_int, 0x100);
            } else {
                word.* |= 0x100;
            }
        }
    }
}
