//! An enemy AI entity's two stat bytes (D2: 7E05C.s; oracle pc/src/battle_helpers.c).
const v = @import("views_battle_helpers");

/// orig: func_800E685C (pc/src/battle_helpers.c)
/// Stores a pair of values on AI entity `id`: the larger at 0xB3, both
/// together (first low, second high) as a halfword at 0x226. An empty slot is
/// ignored. The assembly's return value is not meaningful.
pub fn setActorStatPair(id: c_int, first: c_int, second: c_int) callconv(.c) void {
    const entity: [*]u8 = @ptrCast(v.g.D_800F5878()[@intCast(id)] orelse return);
    entity[0xb3] = @truncate(@as(c_uint, @bitCast(if (first < second) second else first)));
    const packed_pair: c_uint = (@as(c_uint, @bitCast(second)) << 8) | @as(c_uint, @bitCast(first));
    entity[0x226] = @truncate(packed_pair);
    entity[0x227] = @truncate(packed_pair >> 8);
}
