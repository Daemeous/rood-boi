//! Walks over the linked actor list (D2: 73C84.s, 73C24.s, 7E05C.s; oracle pc/src/battle_helpers.c).
const psx = @import("common").psx;
const c = @import("c_battle_helpers");
const v = @import("views_battle_helpers");

/// The next record of the list: the guest pointer stored in the first word.
fn next(node: [*]u8) ?[*]u8 {
    return @ptrFromInt(@as(usize, psx.byteAt(u32, node, 0).*));
}

/// The head of the list: vs_battle_actors[0].
fn first() ?[*]u8 {
    return @ptrCast(v.g.vs_battle_actors()[0]);
}

/// orig: func_800DC810 (pc/src/battle_helpers.c)
/// Runs func_800DD000 (with the context) for every actor, then func_800DD344
/// for every actor. An empty list is reported through pc_unimplemented (the
/// original would fault), which the walks then pass over.
pub fn evaluateActors(context: [*]u8) callconv(.c) void {
    if (first() == null) c.pc_unimplemented("actor evaluation with empty actor list");
    var node = first();
    while (node) |n| : (node = next(n)) {
        c.func_800DD000(psx.at(v.g.D_800F5878(), psx.byteAt(c_int, n, 4).*).*, context);
    }
    node = first();
    while (node) |n| : (node = next(n)) {
        c.func_800DD344(psx.at(v.g.D_800F5878(), psx.byteAt(c_int, n, 4).*).*);
    }
}

/// orig: func_800DC424 (pc/src/battle_helpers.c)
/// Snapshots each listed actor's id and stat words (0x18, 0x1C, 0x948 of its
/// stats at +0x3C, then 0x150, 0x2B4) into the 0x18-byte record at
/// snapshots + id * 0x18. An empty list is reported through pc_unimplemented
/// (the original would fault), which the walk then passes over.
pub fn snapshotActorStats(snapshots: [*]u8) callconv(.c) void {
    if (first() == null) c.pc_unimplemented("actor stat snapshot with empty actor list");
    var node = first();
    while (node) |n| : (node = next(n)) {
        const id = psx.byteAt(c_int, n, 4).*;
        const stats: [*]u8 = @ptrFromInt(@as(usize, psx.byteAt(u32, n, 0x3c).*));
        const record = psx.byteAt(u8, snapshots, @as(isize, id *% 0x18));
        psx.byteAt(c_int, record, 0).* = id;
        psx.byteAt(c_uint, record, 4).* = psx.byteAt(c_uint, stats, 0x18).*;
        psx.byteAt(c_uint, record, 8).* = psx.byteAt(c_uint, stats, 0x1c).*;
        psx.byteAt(c_uint, record, 12).* = psx.byteAt(c_uint, stats, 0x948).*;
        psx.byteAt(c_uint, record, 16).* = psx.byteAt(c_uint, stats, 0x150).*;
        psx.byteAt(c_uint, record, 20).* = psx.byteAt(c_uint, stats, 0x2b4).*;
    }
}

/// orig: func_800E7370 (pc/src/battle_helpers.c)
/// Propagates reciprocal actor relations through the actor list: for every
/// listed actor `id` the state's relation to it (bits 6-7 of the word at
/// 0x1A4 + id * 4) is set from the table D_80069C0C, or to 2 for the state's
/// own id, and the peer is visited in turn while its reciprocal relation (read
/// before the write) is unset.
pub fn propagateRelations(state: [*]u8) callconv(.c) void {
    var node = first();
    while (node) |n| : (node = next(n)) {
        const id = psx.byteAt(c_int, n, 4).*;
        const peer: [*]u8 = @ptrCast(psx.at(v.g.D_800F5878(), id).*.?);
        const self: c_uint = state[0x88];
        var relation: c_uint = 2;
        if (self != @as(c_uint, @bitCast(id))) {
            const column: c_int = @intFromBool(!((id < 1) or (peer[0x2c] >= 3)));
            relation = v.g.D_80069C0C()[@intCast(@as(c_int, state[0x2c]) * 2 + column)];
        }
        const outgoing = psx.byteAt(c_uint, state, 0x1a4 + @as(isize, id *% 4));
        const reciprocal = psx.byteAt(c_uint, peer, 0x1a4 + @as(isize, @intCast(self * 4))).*;
        outgoing.* = (outgoing.* & ~@as(c_uint, 0xc0)) | (relation << 6);
        if (reciprocal & 0xc0 == 0) c.func_800E7370(peer);
    }
}

/// orig: func_800E72D0 (pc/src/battle_helpers.c)
/// Applies each listed actor's packed options (signed halfword at 0x42 of its
/// AI entity) to its relation word at 0x1A4, unless the options are negative;
/// option bit 0 also calls func_800DC210.
pub fn applyActorOptions() callconv(.c) void {
    var node = first();
    while (node) |n| : (node = next(n)) {
        const id = psx.byteAt(c_int, n, 4).*;
        const entity: [*]u8 = @ptrCast(psx.at(v.g.D_800F5878(), id).*.?);
        const options: c_int = psx.byteAt(i16, entity, 0x42).*;
        if (options < 0) continue;
        const flags = psx.byteAt(c_uint, entity, 0x1a4);
        flags.* = (flags.* & 0xc80fff30) | ((@as(c_uint, @bitCast(options >> 3)) & 3) << 28) |
            ((@as(c_uint, @bitCast(options >> 1)) & 3) << 6) | ((@as(c_uint, @bitCast(options >> 6)) & 127) << 20);
        if (options & 1 != 0) c.func_800DC210(entity, 1);
    }
}
