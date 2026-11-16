//! Room section B's handler: forwards its unk34 value to vs_field_setRoomWeather (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8008EA90 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs room section B's handler: unk34 when arg0 is 0 or unk34 is not 1, else 0; returns 1 if the room has section B, else 0.
pub fn func_8008EA90(arg0: c_int) callconv(.c) c_int {
    // The C re-reads sectionB after the null test; that value is dead and the read has no effect, so it is dropped.
    const section = psx.get(v.g.vs_battle_roomData().sectionB) orelse return 0;
    if (arg0 == 0 or section.unk34 != 1) {
        c.vs_field_setRoomWeather(@as(c_int, section.unk34));
    } else {
        c.vs_field_setRoomWeather(0);
    }
    return 1;
}
