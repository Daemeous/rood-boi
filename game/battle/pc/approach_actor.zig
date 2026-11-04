//! Approaching an actor (D2: 72B70.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: func_800DB370 (pc/src/battle_movement.c)
/// Approaches actor `id`. Uses the model's waypoint group when allowed,
/// otherwise its tile; then plans with func_800E24EC and on success enters
/// mode 1. A missing cached actor still enters mode 1 for nonzero ids, as
/// the original's leftover $v0 (id * 4) does.
pub fn approachActor(state: [*]u8, id: c_int, distance: c_int, option: c_int) callconv(.c) c_int {
    const model_slot = psx.at(v.g.D_800F4538(), id).*;
    const cached = psx.at(@as([*]align(1) u32, @ptrFromInt(0x1f800000 + 0x37c)), id).*;
    var result: c_int = undefined;
    if (cached == 0) {
        result = id *% 4;
    } else {
        const model: [*]u8 = @ptrCast(model_slot.?);
        const busy: c_int = psx.byteAt(i16, state, 0x15e).* & 0x38;
        var group: c_int = 0;
        var grouped = false;
        if (state[0xe5] != id) {
            psx.byteAt(u32, state, 0xe4).* = 0;
            psx.byteAt(u32, state, 0xe8).* = 0;
        }
        state[0xe5] = psx.conv(u8, id);
        psx.byteAt(c_int, state, 0xdc).* = distance;
        if (busy == 0 and model[0xa] & 0x1f != 0) {
            const tile = psx.byteAt(u32, model, 0x60).*;
            psx.byteAt(u32, state, 0x168).* = tile;
            if (tile & 0xff00 != 0) {
                group = @as(c_int, @intCast((tile & 0xff00) >> 8)) -% 2;
                grouped = true;
            }
        } else if ((@as(c_int, state[0x29]) | busy) != 0 and model[0xc] & 0xf != 0) {
            group = @as(c_int, model[0xc] & 0xf) -% 2;
            grouped = true;
        } else {
            psx.byteAt(u32, state, 0x168).* = psx.byteAt(u32, model, 0x5c).*;
        }
        if (grouped) {
            const waypoint = c.func_8008E2D4(group);
            c.func_800DFA54(group, waypoint[0], waypoint[2], psx.byteAt(u8, state, 0x168));
            const tile = psx.byteAt(u32, state, 0x168).*;
            psx.byteAt(u32, state, 0xec).* = tile;
            psx.byteAt(u32, state, 0xf4).* = ((tile & 0xff) << 1) | (((tile >> 16) & 0xff) << 1);
        }
        result = c.func_800E24EC(state, option);
    }
    return if (result != 0) c.func_800DB4AC(state, 1) else 0;
}
