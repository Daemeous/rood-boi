//! Entering a movement mode (D2: 72B70.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: vs_battle_aiSetMovementBehavior (pc/src/battle_movement.c)
/// Enters movement mode `mode` (byte 0x89 always records the request).
/// Re-entering the current mode (0x8A) does nothing. Modes reset their own
/// fields; mode 5 picks the nearest flagged waypoint of the actor's set
/// (D_800F58D8, 8 halfwords; bit 15 marks an entry, bits 0-4/5-9 its tile)
/// by Manhattan distance to the actor tile at 0x34. Returns the original
/// $v0, which is always nonzero.
pub fn enterMovementMode(state: *ai.vs_battle_aiState, mode: c_int) callconv(.c) c_int {
    var result: c_int = -1;
    state.unk89 = psx.conv(u8, mode);
    if (@as(c_int, state.field8A) == mode) return mode;
    if (mode == 1) {
        @as(*align(1) u32, @ptrCast(&state.bE4)).* = 0;
        @as(*align(1) u16, @ptrCast(&state.bE8)).* = 0;
        result = 3;
    } else if (mode == 3) {
        state.mode = 0;
        state.cooldown = 0;
        result = 6;
    } else if (mode == 6) {
        state.pending = 0;
        state.previousDestination = -1;
    } else if (mode == 5) {
        var best: c_uint = 0xffffffff;
        const tile = state.tile.raw;
        const tile_x: c_int = @intCast(tile & 0xff);
        const tile_z: c_int = @intCast((tile >> 16) & 0xff);
        var choice: c_int = -1;
        const entries: [*]align(1) const i16 = @ptrFromInt(v.g.D_800F58D8().* +% @as(c_uint, state.row) *% 16);
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
        if (choice >= 0) state.index = psx.conv(u8, choice);
        state.timer104 = 0;
        result = 1;
    }
    state.field8A = psx.conv(u8, mode);
    psx.fieldAt(u8, &state.requests, @as(isize, mode *% 2)).* = 0;
    c.func_800E4BB8(state);
    return result;
}
