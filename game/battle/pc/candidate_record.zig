//! Recording and scoring action candidates (D2: 73C84.s, 73B0C.s, 7C4F4.s; oracle pc/src/battle_helpers.c).
const psx = @import("common").psx;
const c = @import("c_battle_helpers");
const v = @import("views_battle_helpers");

/// orig: func_800DC4F0 (pc/src/battle_helpers.c)
/// Remembers the first candidate for each action slot (word 1: bits 0-2 and
/// 3-4 pick the slot bit) whose action info (halfword at 0xE) has flag 0x40,
/// up to 16 entries copied to the list at D_800F58F8. The slot bit is set
/// even when the list is full.
pub fn recordCandidate(candidate: [*]align(1) c_uint, info: [*]u8) callconv(.c) void {
    const word = candidate[1];
    const bit = @as(c_uint, 1) << @intCast((word & 7) *% 4 +% ((word >> 3) & 3)); // at most 7 * 4 + 3
    if (psx.byteAt(u16, info, 0xe).* & 0x40 == 0 or v.g.D_800F58F4().* & bit != 0) return;
    v.g.D_800F58F4().* |= bit;
    const count = v.g.D_800F58F0().*;
    if (count < 16) {
        v.g.D_800F58F0().* = count +% 1;
        const slot = @as(usize, v.g.D_800F58F8().*) +% @as(usize, @bitCast(@as(isize, count *% 0x14)));
        c.func_800DC2E0(@ptrFromInt(slot), @ptrCast(candidate));
    }
}

/// orig: func_800DC344 (pc/src/battle_helpers.c)
/// Appends a scored candidate (fewer than 16 so far, nonzero weight in bits
/// 19-26 of word 3) to the state's list at 0x2F0, records its pointer at
/// 0x430, and marks (target, action) used in the table at D_800F5904.
pub fn addScoredCandidate(state: [*]u8, candidate: [*]align(1) c_uint) callconv(.c) void {
    const count = psx.byteAt(c_int, state, 0x470).*;
    if (count >= 16 or (candidate[3] >> 19) & 0xff == 0) return;
    const used: [*]u8 = @ptrFromInt(@as(usize, v.g.D_800F5904().*));
    used[((candidate[3] & 0xf) << 8) + ((candidate[1] >> 5) & 0xff)] = 1;
    const record = psx.byteAt(u8, state, 0x2f0 + @as(isize, count *% 0x14));
    psx.byteAt(c_uint, state, 0x430 + @as(isize, count *% 4)).* = psx.addr(record);
    psx.byteAt(c_int, state, 0x470).* = count +% 1;
    c.func_800DC2E0(record, @ptrCast(candidate));
}

/// orig: func_800E4CF4 (pc/src/battle_helpers.c)
/// Whether a candidate may target actor `id`: needs a peer entity and a
/// relation (bits 6-7 of the state's word at 0x1A4 + id * 4) of at least 2;
/// candidate bit 5 adds a relation < 3 check against the action's target
/// mask (unless action flag 0x100); bit 4 rejects relation < 3; slot flag
/// bit 1 rejects targeting oneself.
pub fn canTargetActor(state: [*]u8, candidate: [*]align(1) c_uint, id: c_int) callconv(.c) c_int {
    const peer: [*]u8 = @ptrCast(psx.at(v.g.D_800F5878(), id).* orelse return 0);
    const word = candidate[1];
    const options = candidate[4];
    const slot: c_int = @bitCast((word & 7) *% 4 +% ((word >> 3) & 3));
    const relation = (psx.byteAt(c_uint, state, 0x1a4 + @as(isize, id *% 4)).* >> 6) & 3;
    if (relation < 2) return 0;
    if (options & 0x20 != 0) {
        const peer_flags = psx.byteAt(c_uint, peer, 0x10).*;
        const mask = ((peer_flags >> 7) & 2) | ((peer_flags >> 14) & 4) | (peer_flags & 1);
        const action_flags: c_uint = psx.byteAt(u16, v.g.actions(), @as(isize, @intCast(((word >> 5) & 0xff) * 0x34)) + 0xe).*;
        if (relation >= 3) return 0;
        if (action_flags & 0x100 == 0 and (action_flags >> 9) & mask == 0) return 0;
    }
    if (options & 0x10 != 0 and relation < 3) return 0;
    if ((psx.byteAt(c_uint, state, 0x230 + @as(isize, slot *% 8)).* >> 1) & 1 != 0 and id == state[0x88]) return 0;
    return 1;
}
