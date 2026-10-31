//! Action candidate scoring for the battle AI: the score of one candidate action
//! from every target's status, HP, MP and extra stats (src/BATTLE/BATTLE.PRG/74088.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_74088");
const c = @import("c_74088");

/// orig: actionStatusScore (src/BATTLE/BATTLE.PRG/74088.c)
/// Sums the status weights of target `id`, minus the penalties for no HP or low HP.
fn statusScore(stats: *v.actionStatSnapshot, id: c_int) c_int {
    var sum: c_int = 0;
    const row = psx.at(stats, id);
    const scoreState = psx.globalMany(?*v.actionScoreState, "D_800F5878")[@as(usize, @intCast(row.unknown & 15))].?;
    const noCost = scoreState.maxActionCost == 0;
    var i: c_int = 0;
    while (i < 32) : (i += 1) {
        const bit = @as(u32, 1) << @as(u5, @intCast(i));
        if ((row.status & bit) != 0) {
            if (i != 12 or !noCost) sum +%= @as(c_int, c.D_80069BD0[@intCast(i)]);
        }
    }
    if (row.hp == 0) sum -%= 128;
    if ((@as(c_int, row.hp) << 7) < @as(c_int, row.maxHP) *% 44) sum -%= 84;
    return sum;
}

/// orig: actionHpRatio (src/BATTLE/BATTLE.PRG/74088.c)
/// HP of target `id` as a fraction of 128.
fn hpRatio(stats: *v.actionStatSnapshot, id: c_int) c_int {
    const row = psx.at(stats, id);
    const d: c_int = row.maxHP;
    if (d == 0) return 0;
    return @divTrunc(@as(c_int, row.hp) << 7, d);
}

/// orig: actionMpRatio (src/BATTLE/BATTLE.PRG/74088.c)
/// MP of target `id` as a fraction of 128.
fn mpRatio(stats: *v.actionStatSnapshot, id: c_int) c_int {
    const row = psx.at(stats, id);
    const d: c_int = row.maxMP;
    if (d == 0) return 0;
    return @divTrunc(@as(c_int, row.mp) << 7, d);
}

/// orig: actionExtraRatio (src/BATTLE/BATTLE.PRG/74088.c)
/// The two extra value/limit pairs of target `id`, each as a fraction of 128, summed.
fn extraRatio(stats: *v.actionStatSnapshot, id: c_int) c_int {
    const row = psx.at(stats, id);
    var extra: c_int = 0;
    if (row.limit10 != 0) extra = @divTrunc(@as(c_int, row.value10) << 7, @as(c_int, row.limit10));
    if (row.limit14 != 0) extra +%= @divTrunc(@as(c_int, row.value14) << 7, @as(c_int, row.limit14));
    return extra;
}

/// orig: func_800DCC94 (src/BATTLE/BATTLE.PRG/74088.c)
/// Scores one candidate action against every battle target and writes the result to the candidate.
pub fn func_800DCC94(state: ?*v.actionScoreState, candidate: ?*v.actionCandidateEntry, mode: c_int, action: ?*v.actionScoreMetadata) callconv(.c) void {
    var special: c_int = 0;
    var flags: c_int = 0;
    if (action) |a| flags = @as(c_int, a.flags);
    if ((flags & 32) != 0 and (c.rand() & 127) < 115) {
        candidate.?.score = std.math.minInt(c_int);
        return;
    }
    const cand = candidate.?;
    cand.score = 0;
    psx.setBits(cand, "weight", 128);
    const stats: *v.actionStatSnapshot = @ptrFromInt(0x1F80018C);
    c.func_800DC424(psx.cptr(c.actionStatSnapshot, stats));
    const st = state.?;
    if (st.special != 0 and (flags & 16) != 0) {
        special = 1;
        psx.setBits(cand, "weight", 40);
    } else if (mode != 0) {
        c.func_800DCAA0(@ptrCast(st), @as(c_int, @bitCast(psx.addr(stats))), @ptrCast(cand), mode);
        c.func_800DCBD8(@ptrCast(cand));
    }
    var actor: ?*v.actionScoreActor = @ptrCast(v.g.vs_battle_actors()[0]);
    while (actor != null) : (actor = psx.get(actor.?.next)) {
        const a = actor.?;
        const id: c_int = a.id;
        const kind: c_uint = psx.at(&st.targets, id).classification >> 6;
        if (kind == 1) continue;
        var value: c_int = c.func_800DC284(
            statusScore(stats, id),
            hpRatio(stats, id),
            mpRatio(stats, id),
            extraRatio(stats, id),
        );
        if (kind != 2) value = -%value;
        cand.score +%= value;
    }
    if (special != 0) cand.score +%= 1;
    cand.score = (cand.score << 7) +% @as(c_int, psx.getBits(cand, "weight"));
}
