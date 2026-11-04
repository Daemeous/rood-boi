//! Whether a candidate action can start now (D2: 73580.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: func_800DBD80 (pc/src/battle_movement.c)
/// Whether candidate action `candidate` can start now (weight, threshold
/// against the room value at D_800F58BC + 0x18, availability, target
/// engagement, distance and range, relation and model flags). The original
/// calls vs_gte_rsqrt on the distance and discards the result; the call is
/// kept for its GTE side effects.
pub fn candidateStartable(state: [*]u8, candidate: [*]u8) callconv(.c) c_int {
    const weight: c_uint = psx.byteAt(u32, candidate, 12).*;
    const target: usize = weight & 0xf;
    if ((weight >> 19) & 0xff == 0) return 0;
    const room: [*]u8 = @ptrFromInt(v.g.D_800F58BC().*);
    if (psx.byteAt(u16, room, 0x18).* < (psx.byteAt(u32, candidate, 4).* >> 13) & 0xffff) return 0;
    if (c.func_800DBCB4(state, candidate) == 0) return 0;
    const limit: c_int = psx.byteAt(c_int, state, @intCast(0x1e4 + target * 4)).*; // target < 16
    if (v.g.D_800F45E0()[target] != null) {
        if (0x19000 < limit or state[0x13] != 0 or c.func_800DBCEC(state, @intCast(target)) != 0) return 0;
    }
    const distance: c_int = @bitCast(psx.byteAt(u32, candidate, 8).*);
    const relation: c_int = psx.byteAt(c_int, state, @intCast(0x1a4 + target * 4)).*;
    if (distance < limit) return 0;
    _ = c.vs_gte_rsqrt(distance);
    const options = psx.byteAt(u32, candidate, 16).*;
    const range: c_int = @bitCast((options << 10) >> 16);
    if (range > 0 and options & 2 != 0 and
        limit < @as(c_int, @bitCast(@as(c_uint, @bitCast(range)) *% @as(c_uint, @bitCast(range))))) return 0;
    if (relation >= 0) return 0;
    const model: c_uint = @as([*]u8, @ptrCast(v.g.D_800F4538()[0].?))[0xa];
    if (model & 7 != 0 or (model & 0x18) == 8) return 0;
    if ((((options >> 2) & 1) | @as(c_uint, @bitCast((relation >> 2) & 1))) == 0) return 0;
    if ((((options >> 3) & 1) | @as(c_uint, @intFromBool(((relation >> 27) & 1) == 0))) == 0) return 0;
    return 1;
}
