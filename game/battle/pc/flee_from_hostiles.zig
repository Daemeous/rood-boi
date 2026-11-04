//! Fleeing from hostile actors (D2: 72B70.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: func_800DB61C (pc/src/battle_movement.c)
/// Flees toward the (logically halved, running) average tile of the hostile
/// actors (relation bits 5-7 == 7), remembering it for 90 frames at
/// 0x110/0x114; while in mode 4 keeps going there until the timer runs out.
pub fn fleeFromHostiles(state: [*]u8, kind: c_int) callconv(.c) c_int {
    var sum_x: c_uint = 0;
    var sum_z: c_uint = 0;
    var count: c_int = 0;
    var node: ?[*]u8 = if (v.g.vs_battle_actors()[0]) |first| @ptrCast(first) else null;
    while (node) |n| : (node = @ptrFromInt(psx.byteAt(u32, n, 0).*)) {
        const id = psx.byteAt(c_int, n, 4).*;
        if (((psx.byteAt(u32, state, 0x1a4 +% @as(isize, id *% 4)).* >> 5) & 7) < 7) continue;
        const model: [*]u8 = @ptrCast(psx.at(v.g.D_800F4538(), id).*.?);
        count += 1; // one per actor in the list
        sum_x +%= psx.conv(c_uint, @as(c_int, psx.byteAt(i16, model, 0x1c).*));
        sum_z +%= psx.conv(c_uint, @as(c_int, psx.byteAt(i16, model, 0x20).*));
        if (count != 1) {
            sum_x >>= 1;
            sum_z >>= 1;
        }
    }
    if (count > 0) {
        psx.byteAt(u16, state, 0x114).* = 90;
        psx.byteAt(u32, state, 0x110).* = (sum_x >> 7) | ((sum_z >> 7) << 16);
    } else {
        var timer: c_uint = psx.byteAt(u16, state, 0x114).*;
        if (timer != 0) timer -%= 1;
        psx.byteAt(u16, state, 0x114).* = psx.conv(u16, timer);
        if (timer == 0 or state[0x8a] != 4) return 0;
    }
    const tile = psx.byteAt(u32, state, 0x110).*;
    state[4] = 1;
    psx.byteAt(u32, state, 0x17c).* = (tile & 0xff) *% 128 +% 64;
    psx.byteAt(u32, state, 0x180).* = ((tile >> 16) & 0xff) *% 128 +% 64;
    psx.byteAt(u32, state, 0x168).* = tile;
    c.func_800E4FE0(state, kind);
    return c.func_800DB4AC(state, 4);
}
