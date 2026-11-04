//! Remembering an actor's best target (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: func_800E4C8C (pc/src/battle_movement.c)
/// Remembers the best candidate's target when there is one (0x470 positive)
/// and the actor is not locked (0x19): 0x38 = target << 24 | current tile,
/// 0x3C = the target's tile, 0x40 = 900-frame memory.
pub fn rememberTarget(state: [*]u8) callconv(.c) void {
    if (@as(c_int, @bitCast(psx.byteAt(c_uint, state, 0x470).*)) <= 0 or state[0x19] != 0) return;
    const candidate: [*]const c_uint = @ptrFromInt(psx.byteAt(c_uint, state, 0x430).*);
    const target: c_uint = candidate[3] & 0xf;
    psx.byteAt(c_uint, state, 0x38).* = (target << 24) | (psx.byteAt(c_uint, state, 0x34).* & 0xffffff);
    const model: [*]u8 = @ptrCast(v.g.D_800F4538()[target].?);
    psx.byteAt(c_uint, state, 0x3c).* = psx.byteAt(c_uint, model, 0x5c).*;
    psx.byteAt(u16, state, 0x40).* = 900;
}
