//! Marking an actor as a target of the state (D2: 73C84.s; oracle pc/src/battle_helpers.c).
const psx = @import("common").psx;
const c = @import("c_battle_helpers");

/// orig: func_800DC784 (pc/src/battle_helpers.c)
/// Marks target `id` (word 0x1A4 + id * 4) as classification 3 (bits 6-7)
/// and clears bits 8-14 (flags & 0xFFFF803F | 0xC0). Unless it already was 3,
/// the actor first reacts: rand() is drawn (its result unused),
/// byte 0x18 is set to 1, then func_800D8260(state, 6, 0x28) and
/// func_800E48A8(state, 2, 0x80). The old word is kept across those calls.
pub fn markTargetHostile(state: [*]u8, id: c_int) callconv(.c) void {
    const target = psx.byteAt(c_uint, state, 0x1a4 + @as(isize, id *% 4));
    const flags = target.*;
    if (((flags >> 6) & 3) < 3) {
        _ = c.rand();
        state[0x18] = 1;
        c.func_800D8260(state, 6, 0x28);
        c.func_800E48A8(state, 2, 0x80);
    }
    target.* = (flags & 0xffff803f) | 0xc0;
}
