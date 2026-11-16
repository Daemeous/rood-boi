//! Marks the room section under a position and runs its callback
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_disableTrapAtPosition (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Finds the room section at the position's 128-unit cell; if there is one, marks it and runs its callback with its two coordinates.
pub fn vs_field_disableTrapAtPosition(arg0: ?*v.func_8006CE70_t) callconv(.c) void {
    const pos = arg0.?;
    const section_c = c.vs_field_findRoomTriggerAtTile(
        @divTrunc(@as(c_int, psx.conv(i16, pos.unk0)), 128),
        @divTrunc(@as(c_int, pos.unk4), 128),
        0,
    );

    if (section_c != null) {
        const section: *v._mpdRoomSection9 = @ptrCast(section_c);
        c.vs_field_setRoomTriggerEnabled(section_c, 1);
        c.vs_battle_aiMarkAiTileDynamicObstacle(@as(c_int, section.unk0), @as(c_int, section.unk2));
    }
}
