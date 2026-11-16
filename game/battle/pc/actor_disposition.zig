//! An actor's disposition word (D2: 7DE94.s; oracle pc/src/battle_helpers.c).
const ai = @import("views_71618");
const psx = @import("common").psx;
const v = @import("views_battle_helpers");

/// orig: vs_battle_aiPackActorAiPersistenceFlags (pc/src/battle_helpers.c)
/// Packs an actor's disposition for unloading a room: byte 0x13, relation
/// bits 6-7 of the word at 0x1A4 (to bits 1-2), bits 28-29 (to 3-4), whether
/// byte 0x12D is zero (bit 5) and bits 20-26 (to 6-12). An empty slot gives -1.
pub fn dispositionWord(id: c_int) callconv(.c) c_short {
    const entity: *ai.vs_battle_aiState = @ptrCast(@alignCast(psx.at(v.g.vs_battle_aiActorAiStates(), id).* orelse return -1));
    const flags = entity.tbl.targetFlags[0];
    const word: c_uint = entity.flag13 | (((flags >> 6) & 3) << 1) | (((flags >> 28) & 3) << 3) |
        (@as(c_uint, @intFromBool(entity.mode12D == 0)) << 5) | (((flags >> 20) & 127) << 6);
    return psx.conv(i16, word);
}
