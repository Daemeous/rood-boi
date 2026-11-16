//! Marks the current scene's area and room as seen (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _setRoomSeen (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets the area and room seen flags for the current scene and room, once per pending request.
pub fn setRoomSeen() callconv(.c) void {
    if (v.g.vs_field_isRoomVisitPending().* == 0) {
        return;
    }

    const scene: c_uint = psx.conv(c_uint, c.vs_battle_getCurrentSceneId());
    const status = v.g.vs_main_mapStatus();
    // The index is not checked against the array in the C: psx.at keeps the write where the C puts it.
    const area = psx.at(&status.areaFlags, scene >> 5);
    area.* |= @bitCast(@as(c_uint, 1) << @intCast(scene & 0x1F));

    const room: c_uint = psx.conv(c_uint, c.vs_battle_getCurrentRoomId());
    v.g.vs_field_isRoomVisitPending().* = 0;
    const rooms = psx.at(&status.roomFlags, room >> 5);
    rooms.* |= @bitCast(@as(c_uint, 1) << @intCast(room & 0x1F));
}
