//! Aiming at an actor (D2: 72B70.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: func_800DB74C (pc/src/battle_movement.c)
/// Aims at actor `id`'s position (0x17C/0x180) and tile (0x168); on a plan
/// (func_800E2B2C) enters mode 6.
pub fn aimAtActor(state: [*]u8, id: c_int) callconv(.c) c_int {
    const model: [*]u8 = @ptrCast(psx.at(v.g.D_800F4538(), id).*.?);
    psx.byteAt(c_int, state, 0x17c).* = psx.byteAt(i16, model, 0x1c).*;
    psx.byteAt(c_int, state, 0x180).* = psx.byteAt(i16, model, 0x20).*;
    psx.byteAt(u32, state, 0x168).* = psx.byteAt(u32, model, 0x5c).*;
    return if (c.func_800E2B2C(state, id) != 0) c.func_800DB4AC(state, 6) else 0;
}
