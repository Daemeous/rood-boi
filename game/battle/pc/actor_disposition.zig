//! An actor's disposition word (D2: 7DE94.s; oracle pc/src/battle_helpers.c).
const psx = @import("common").psx;
const v = @import("views_battle_helpers");

/// orig: func_800E6694 (pc/src/battle_helpers.c)
/// Packs an actor's disposition for unloading a room: byte 0x13, relation
/// bits 6-7 of the word at 0x1A4 (to bits 1-2), bits 28-29 (to 3-4), whether
/// byte 0x12D is zero (bit 5) and bits 20-26 (to 6-12). An empty slot gives -1.
pub fn dispositionWord(id: c_int) callconv(.c) c_short {
    const entity: [*]u8 = @ptrCast(psx.at(v.g.D_800F5878(), id).* orelse return -1);
    const flags = psx.byteAt(c_uint, entity, 0x1a4).*;
    const word: c_uint = entity[0x13] | (((flags >> 6) & 3) << 1) | (((flags >> 28) & 3) << 3) |
        (@as(c_uint, @intFromBool(entity[0x12d] == 0)) << 5) | (((flags >> 20) & 127) << 6);
    return psx.conv(i16, word);
}
