//! Advances the battle timer by the tick speed while its hold flag is clear (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_field_advanceRoomAnimationClock (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Adds the tick speed to the battle timer unless vs_field_areRoomUpdatesPaused is set.
pub fn vs_field_advanceRoomAnimationClock() callconv(.c) void {
    if (v.g.vs_field_areRoomUpdatesPaused().* == 0) {
        const sum: c_int = @as(c_int, v.g.vs_field_roomAnimationTicks().*) +% v.g.vs_gametime_tickspeed().*;
        v.g.vs_field_roomAnimationTicks().* = psx.conv(u16, sum);
    }
}
