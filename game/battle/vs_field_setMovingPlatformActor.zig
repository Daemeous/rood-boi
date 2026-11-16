//! Room section 12: sets one entry's 7-bit flag and clears its counter when shown (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_field_setMovingPlatformActor (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets room section 12 entry `index`'s 7-bit field to `value`, clearing its counter when `value` is not negative; returns 0 when the room has no section 12.
pub fn vs_field_setMovingPlatformActor(index: c_int, value: c_int) callconv(.c) c_int {
    if (psx.get(v.g.vs_battle_roomData().section12)) |section12| {
        psx.setBits(psx.at(section12, index), "unk4_1", psx.conv(u7, value));
        if (value >= 0 and v.g.vs_field_platformFreezeFrames().* == 0) {
            psx.at(v.g.vs_field_platformBounceFrames(), index).* = 0;
        }
        return 1;
    }
    return 0;
}
