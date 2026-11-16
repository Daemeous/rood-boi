//! Walks over the linked actor list (D2: 73C84.s, 73C24.s, 7E05C.s; oracle pc/src/battle_helpers.c).
const std = @import("std");
const ai = @import("views_71618");
const psx = @import("common").psx;
const c = @import("c_battle_helpers");
const v = @import("views_battle_helpers");

/// The next record of the list: the guest pointer stored in the first word.
fn next(node: *ai.vs_battle_actor) ?*ai.vs_battle_actor {
    return @ptrCast(node.next);
}

/// The head of the list: vs_battle_actors[0].
fn first() ?*ai.vs_battle_actor {
    return @ptrCast(@alignCast(v.g.vs_battle_actors()[0]));
}

/// orig: vs_battle_aiApplyActionRelationshipChanges (pc/src/battle_helpers.c)
/// Runs vs_battle_aiApplyActorActionReaction (with the context) for every actor, then vs_battle_aiPropagateActorRelationships
/// for every actor. An empty list is reported through pc_unimplemented (the
/// original would fault), which the walks then pass over.
pub fn evaluateActors(context: [*]u8) callconv(.c) void {
    if (first() == null) c.pc_unimplemented("actor evaluation with empty actor list");
    var node = first();
    while (node) |n| : (node = next(n)) {
        c.vs_battle_aiApplyActorActionReaction(psx.at(v.g.vs_battle_aiActorAiStates(), n.id).*, context);
    }
    node = first();
    while (node) |n| : (node = next(n)) {
        c.vs_battle_aiPropagateActorRelationships(psx.at(v.g.vs_battle_aiActorAiStates(), n.id).*);
    }
}

comptime {
    // The words the original copied: 0x18, 0x1C (HP and MP pairs), 0x948, 0x150 (weapon DP pair), 0x2B4 (shield DP pair).
    std.debug.assert(@offsetOf(ai.vs_battle_actor2, "currentHP") == 0x18 and @offsetOf(ai.vs_battle_actor2, "currentMP") == 0x1c);
    std.debug.assert(@offsetOf(ai.vs_battle_actor2, "statuses") == 0x948);
    std.debug.assert(@offsetOf(ai.vs_battle_actor2, "weapon") + @offsetOf(ai.vs_battle_uiWeapon, "currentDp") == 0x150);
    std.debug.assert(@offsetOf(ai.vs_battle_actor2, "shield") + @offsetOf(ai.vs_battle_uiShield, "currentDp") == 0x2b4);
}

/// A word of two 16-bit stats as the original loaded it: the first in the low half, the second in the high.
fn halves(low: anytype, high: anytype) c_uint {
    const L = @Int(.unsigned, @bitSizeOf(@TypeOf(low)));
    const H = @Int(.unsigned, @bitSizeOf(@TypeOf(high)));
    return @as(c_uint, @as(L, @bitCast(low))) | (@as(c_uint, @as(H, @bitCast(high))) << 16);
}

/// orig: vs_battle_aiCaptureActorConditionTable (pc/src/battle_helpers.c)
/// Snapshots each listed actor's id and stat words (0x18, 0x1C, 0x948 of its
/// stats at +0x3C, then 0x150, 0x2B4) into the 0x18-byte record at
/// snapshots + id * 0x18. An empty list is reported through pc_unimplemented
/// (the original would fault), which the walk then passes over.
pub fn snapshotActorStats(snapshots: [*]u8) callconv(.c) void {
    if (first() == null) c.pc_unimplemented("actor stat snapshot with empty actor list");
    var node = first();
    while (node) |n| : (node = next(n)) {
        const id = n.id;
        const stats = n.stats.?;
        const record = psx.byteAt(u8, snapshots, @as(isize, id *% 0x18));
        psx.byteAt(c_int, record, 0).* = id;
        psx.byteAt(c_uint, record, 4).* = halves(stats.currentHP, stats.maxHP);
        psx.byteAt(c_uint, record, 8).* = halves(stats.currentMP, stats.maxMP);
        psx.byteAt(c_uint, record, 12).* = @bitCast(stats.statuses);
        psx.byteAt(c_uint, record, 16).* = halves(stats.weapon.currentDp, stats.weapon.maxDp);
        psx.byteAt(c_uint, record, 20).* = halves(stats.shield.currentDp, stats.shield.maxDp);
    }
}

/// orig: vs_battle_aiInitializeActorAiRelations (pc/src/battle_helpers.c)
/// Propagates reciprocal actor relations through the actor list: for every
/// listed actor `id` the state's relation to it (bits 6-7 of the word at
/// 0x1A4 + id * 4) is set from the table D_80069C0C, or to 2 for the state's
/// own id, and the peer is visited in turn while its reciprocal relation (read
/// before the write) is unset.
pub fn propagateRelations(state: *ai.vs_battle_aiState) callconv(.c) void {
    var node = first();
    while (node) |n| : (node = next(n)) {
        const id = n.id;
        const peer: *ai.vs_battle_aiState = @ptrCast(@alignCast(psx.at(v.g.vs_battle_aiActorAiStates(), id).*.?));
        const self: c_uint = state.id;
        var relation: c_uint = 2;
        if (self != @as(c_uint, @bitCast(id))) {
            const column: c_int = @intFromBool(!((id < 1) or (peer.f2C >= 3)));
            relation = v.g.D_80069C0C()[@intCast(@as(c_int, state.f2C) * 2 + column)];
        }
        const outgoing = psx.fieldAt(c_uint, &state.tbl.t.targets.bits32, @as(isize, id *% 4));
        const reciprocal = psx.fieldAt(c_uint, &peer.tbl.t.targets.bits32, @as(isize, @intCast(self * 4))).*;
        outgoing.* = (outgoing.* & ~@as(c_uint, 0xc0)) | (relation << 6);
        if (reciprocal & 0xc0 == 0) c.vs_battle_aiInitializeActorAiRelations(peer);
    }
}

/// orig: func_800E72D0 (pc/src/battle_helpers.c)
/// Applies each listed actor's packed options (signed halfword at 0x42 of its
/// AI entity) to its relation word at 0x1A4, unless the options are negative;
/// option bit 0 also calls vs_battle_aiTryBeginAshleyAlliance.
pub fn applyActorOptions() callconv(.c) void {
    var node = first();
    while (node) |n| : (node = next(n)) {
        const id = n.id;
        const entity: *ai.vs_battle_aiState = @ptrCast(@alignCast(psx.at(v.g.vs_battle_aiActorAiStates(), id).*.?));
        const options: c_int = entity.options;
        if (options < 0) continue;
        const flags = &entity.tbl.targetFlags[0];
        flags.* = (flags.* & 0xc80fff30) | ((@as(c_uint, @bitCast(options >> 3)) & 3) << 28) |
            ((@as(c_uint, @bitCast(options >> 1)) & 3) << 6) | ((@as(c_uint, @bitCast(options >> 6)) & 127) << 20);
        if (options & 1 != 0) c.vs_battle_aiTryBeginAshleyAlliance(entity, 1);
    }
}
