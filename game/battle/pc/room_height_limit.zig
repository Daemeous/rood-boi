//! The room's height limit (D2: 7E05C.s; oracle pc/src/actor_cleanup.c).
const v = @import("views_actor_cleanup");
const psx = @import("common").psx;

/// orig: vs_battle_aiRoomNavigationCeilingHeight (pc/src/actor_cleanup.c)
/// Word 0x14 of the vs_battle_aiRoomAiState record, or 0 when no record is loaded.
pub fn roomHeightLimit() callconv(.c) c_int {
    return if (v.g.vs_battle_aiRoomAiState().*) |room| room.unk14 else 0;
}
