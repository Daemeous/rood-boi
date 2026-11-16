//! Clears the battle room data when the room being left is the one loaded
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_releaseOwnedRoomSections (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Unloads the battle room if it is loaded as room `arg0`: frees its geometry section and clears the room record. Returns 1 if it did.
pub fn vs_field_releaseOwnedRoomSections(arg0: c_int) callconv(.c) c_int {
    const room = v.g.vs_battle_roomData();
    if (room.unk0 == 1) {
        if (@as(c_int, room.unk2) == arg0) {
            _ = c.vs_field_releaseTiledBackground();
            c.vs_field_releaseWeatherParticles();
            c.vs_field_releaseRoomEffects();
            if (psx.get(room.geometrySection) != null) {
                c.vs_main_freeHeap(psx.get(room.geometrySection));
            }
            c.vs_main_bzero(@ptrCast(room), 0xC4);
            return 1;
        }
    }
    return 0;
}
