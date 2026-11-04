//! Entering a movement mode (D2: 72B70.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: func_800DB4AC (pc/src/battle_movement.c)
/// Enters movement mode `mode` (byte 0x89 always records the request).
/// Re-entering the current mode (0x8A) does nothing. Modes reset their own
/// fields; mode 5 picks the nearest flagged waypoint of the actor's set
/// (D_800F58D8, 8 halfwords; bit 15 marks an entry, bits 0-4/5-9 its tile)
/// by Manhattan distance to the actor tile at 0x34. Returns the original
/// $v0, which is always nonzero.
pub fn enterMovementMode(state: [*]u8, mode: c_int) callconv(.c) c_int {
    var result: c_int = -1;
    state[0x89] = psx.conv(u8, mode);
    if (@as(c_int, state[0x8a]) == mode) return mode;
    if (mode == 1) {
        psx.byteAt(u32, state, 0xe4).* = 0;
        psx.byteAt(u16, state, 0xe8).* = 0;
        result = 3;
    } else if (mode == 3) {
        psx.byteAt(u16, state, 0xc0).* = 0;
        psx.byteAt(u16, state, 0xd2).* = 0;
        result = 6;
    } else if (mode == 6) {
        state[0x10c] = 0;
        psx.byteAt(c_int, state, 0x108).* = -1;
    } else if (mode == 5) {
        var best: c_uint = 0xffffffff;
        const tile = psx.byteAt(u32, state, 0x34).*;
        const tile_x: c_int = @intCast(tile & 0xff);
        const tile_z: c_int = @intCast((tile >> 16) & 0xff);
        var choice: c_int = -1;
        const entries: [*]align(1) const i16 = @ptrFromInt(v.g.D_800F58D8().* +% @as(c_uint, state[0x101]) *% 16);
        var i: c_int = 0;
        while (i < 8) : (i += 1) { // i < 8: the entry count
            const entry: c_int = entries[@intCast(i)];
            if (entry >= 0) continue;
            const dx: c_int = (entry & 0x1f) -% tile_x;
            const dz: c_int = @as(c_int, @bitCast((@as(c_uint, @bitCast(entry)) >> 5) & 0x1f)) -% tile_z;
            const distance: c_uint = @bitCast((if (dx < 0) -%dx else dx) +% (if (dz < 0) -%dz else dz));
            if (distance < best) {
                best = distance;
                choice = i;
            }
        }
        if (choice >= 0) state[0x100] = psx.conv(u8, choice);
        state[0x104] = 0;
        result = 1;
    }
    state[0x8a] = psx.conv(u8, mode);
    psx.byteAt(u8, state, @as(isize, 0x474 +% mode *% 2)).* = 0;
    c.func_800E4BB8(state);
    return result;
}
