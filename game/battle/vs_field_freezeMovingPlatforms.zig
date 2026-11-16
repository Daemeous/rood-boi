//! Sets the flag at vs_field_platformFreezeFrames from the argument and, when the current room has a section 12, calls vs_field_setPlatformFreezeAppearance (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_freezeMovingPlatforms (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores `flag` in vs_field_platformFreezeFrames when the room's section 12 holds at least one 184-byte entry, calling vs_field_setPlatformFreezeAppearance with it.
pub fn vs_field_freezeMovingPlatforms(flag: c_int) callconv(.c) void {
    if (v.g.vs_battle_roomData().header.section12Len / 184 != 0) {
        if (flag != 0) {
            c.vs_field_setPlatformFreezeAppearance(1);
        } else {
            c.vs_field_setPlatformFreezeAppearance(0);
        }
        v.g.vs_field_platformFreezeFrames().* = psx.conv(i16, flag);
    }
}
