//! Fleeing from hostile actors (D2: 72B70.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: vs_battle_aiFleeVisibleEnemies (pc/src/battle_movement.c)
/// Flees toward the (logically halved, running) average tile of the hostile
/// actors (relation bits 5-7 == 7), remembering it for 90 frames at
/// 0x110/0x114; while in mode 4 keeps going there until the timer runs out.
pub fn fleeFromHostiles(state: *ai.vs_battle_aiState, kind: c_int) callconv(.c) c_int {
    var sum_x: c_uint = 0;
    var sum_z: c_uint = 0;
    var count: c_int = 0;
    var node: ?*ai.vs_battle_actor = if (v.g.vs_battle_actors()[0]) |first| @ptrCast(@alignCast(first)) else null;
    while (node) |n| : (node = @ptrCast(n.next)) {
        const id = n.id;
        if (((psx.fieldAt(u32, &state.tbl.t.targets.bits32, @as(isize, id *% 4)).* >> 5) & 7) < 7) continue;
        const model: [*]u8 = @ptrCast(psx.at(v.g.vs_actors_actorRecords(), id).*.?);
        count += 1; // one per actor in the list
        sum_x +%= psx.conv(c_uint, @as(c_int, psx.byteAt(i16, model, 0x1c).*));
        sum_z +%= psx.conv(c_uint, @as(c_int, psx.byteAt(i16, model, 0x20).*));
        if (count != 1) {
            sum_x >>= 1;
            sum_z >>= 1;
        }
    }
    if (count > 0) {
        state.unk114 = 90;
        state.field110 = @bitCast((sum_x >> 7) | ((sum_z >> 7) << 16));
    } else {
        var timer: c_uint = state.unk114;
        if (timer != 0) timer -%= 1;
        state.unk114 = psx.conv(u16, timer);
        if (timer == 0 or state.field8A != 4) return 0;
    }
    const tile = state.field110;
    state.flags4.b[0] = 1;
    state.moveX = (tile & 0xff) *% 128 +% 64;
    state.moveZ = ((tile >> 16) & 0xff) *% 128 +% 64;
    state.destination168.destination = @bitCast(tile);
    c.func_800E4FE0(state, kind);
    return c.vs_battle_aiSetMovementBehavior(state, 4);
}
