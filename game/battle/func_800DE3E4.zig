//! Plans an AI actor's next actions: scores candidates at three look-ahead
//! delays on a snapshot of every actor's stats, keeps a quota from each, and
//! spreads their thresholds (src/BATTLE/BATTLE.PRG/74088.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_74088");
const c = @import("c_74088");

fn statsCopies() *?[*]v.vs_battle_actor2 {
    return psx.global(?[*]v.vs_battle_actor2, "D_800F58E8");
}

/// orig: snapshotActorStats (src/BATTLE/BATTLE.PRG/74088.c)
/// Copies every actor's stats into a heap block (0 when the heap is full).
fn snapshotActorStats() c_int {
    statsCopies().* = @ptrCast(@alignCast(c.vs_main_allocHeapR(@as(c_uint, @sizeOf(v.vs_battle_actor2) * 16))));
    if (statsCopies().* == null) {
        return 0;
    }
    var actor = v.g.vs_battle_actors()[0];
    while (actor) |a| : (actor = psx.get(a.next)) {
        c.func_800DC3E8(@ptrCast(psx.get(a.stats)), @ptrCast(psx.at(statsCopies().*.?, a.id)));
    }
    return 1;
}

/// orig: restoreActorStats (src/BATTLE/BATTLE.PRG/74088.c)
/// Copies the snapshot back into every actor's stats and frees it.
fn restoreActorStats() void {
    var actor = v.g.vs_battle_actors()[0];
    while (actor) |a| : (actor = psx.get(a.next)) {
        c.func_800DC3E8(@ptrCast(psx.at(statsCopies().*.?, a.id)), @ptrCast(psx.get(a.stats)));
    }
    c.vs_main_freeHeapR(@ptrCast(statsCopies().*));
}

/// The c module's prototypes of these take actionPlanState / actionCandidateState, which
/// translate-c cannot lay out (an array of a bitfield struct): called by name with the C's arguments.
fn planFn(comptime name: []const u8) *const fn (?*anyopaque) callconv(.c) void {
    return @extern(*const fn (?*anyopaque) callconv(.c) void, .{ .name = name });
}

fn entryAt(state: *v.actionPlanState, i: c_int) *v.actionCandidateEntry {
    return psx.get(psx.at(&state.entries, i).*).?;
}

/// orig: func_800DE3E4 (src/BATTLE/BATTLE.PRG/74088.c)
/// Chooses up to 16 action candidates for `state`'s actor (10, then the rest, then 3, from three delays), with a fallback list.
pub fn func_800DE3E4(state: ?*v.actionPlanState) callconv(.c) void {
    const delays = [3]c_int{ 0, 455, 2900 };
    const quotas = [3]c_int{ 10, 3, 3 };
    // Uninitialised in the original: read by the `count >= 2` test after a
    // failed snapshot (stack bytes there; 0 here).
    var selected = [3]c_int{ 0, 0, 0 };
    const s = state.?;
    const stats = psx.get(s.stats);
    const pass1 = psx.global(?[*][16]v.actionCandidateEntry, "D_800F58EC");
    const fallbacks = psx.global(?[*]v.actionCandidateEntry, "D_800F58F8");
    const used = psx.global(?[*][256]u8, "D_800F5904");

    psx.global(c_int, "D_800F58F0").* = 0;
    psx.global(c_int, "D_800F58F4").* = 0;

    var row: usize = 0;
    while (row < 6) : (row += 1) {
        var column: usize = 0;
        while (column < 4) : (column += 1) {
            var actor = v.g.vs_battle_actors()[0];
            while (actor) |a| : (actor = psx.get(a.next)) {
                psx.at(psx.at(used.*.?, a.id), @as(u16, @bitCast(stats.?.armor[row][column].id))).* = 0;
            }
        }
    }

    s.count = 0;
    if (s.id == 0) {
        return;
    }

    if (snapshotActorStats() != 0) {
        const fromStats: c_int = @divTrunc(@as(c_int, psx.get(s.stats).?.unk954), 35);
        const minDelay: c_int = if (@as(c_int, s.minDelay) < fromStats) fromStats else s.minDelay;
        v.g.D_800F58BC().*.?.unk18 = 0;

        var pass: usize = 0;
        while (pass < 3) : (pass += 1) {
            const delay: c_int = if (delays[pass] < minDelay) minDelay else delays[pass];
            c.func_800DE030(delay);
            @extern(*const fn (?*anyopaque, c_int) callconv(.c) void, .{ .name = "func_800DD918" })(s, psx.conv(i16, delay));
            planFn("func_800DC3CC")(s);

            if (pass == 1) {
                var remaining: c_int = 16 -% selected[0] -% quotas[2];
                if (remaining < 0) {
                    remaining = 0;
                }
                selected[1] = remaining;
                selected[pass] = if (selected[1] > s.count) s.count else selected[1];
            } else {
                selected[pass] = if (s.count > quotas[pass]) quotas[pass] else s.count;
            }

            var i: c_int = 0;
            while (i < selected[pass]) : (i += 1) {
                c.func_800DC2E0(@ptrCast(psx.at(&pass1.*.?[pass], i)), @ptrCast(entryAt(s, i)));
            }
            i = selected[pass];
            while (i < s.count) : (i += 1) {
                const rejected = entryAt(s, i);
                psx.at(psx.at(used.*.?, psx.getBits(rejected, "targetId")), psx.getBits(rejected, "action")).* = 0;
            }
            s.count = 0;
        }

        restore: {
            pass = 0;
            while (pass < 3) : (pass += 1) {
                var j: c_int = 0;
                while (j < selected[pass]) : (j += 1) {
                    const index = s.count;
                    const entry = psx.at(&s.candidates, index);
                    c.func_800DC2E0(@ptrCast(entry), @ptrCast(psx.at(&pass1.*.?[pass], j)));
                    psx.put(psx.at(&s.entries, index), entry);
                    s.count = index +% 1;
                    if (index +% 1 >= 16) {
                        break :restore;
                    }
                }
            }

            if (s.count == 0 and s.allowFallback != 0) {
                var i: c_int = 0;
                while (i < psx.global(c_int, "D_800F58F0").*) : (i += 1) {
                    const fallback = psx.at(&s.candidates, i);
                    c.func_800DC2E0(@ptrCast(fallback), @ptrCast(psx.at(fallbacks.*.?, i)));
                    psx.setBits(fallback, "weight", 128);
                    psx.put(psx.at(&s.entries, i), fallback);
                }
                s.count = psx.global(c_int, "D_800F58F0").*;
                planFn("func_800DC3CC")(s);
            }
        }
        restoreActorStats();
        v.g.D_800F58BC().*.?.unk18 = 0;
    } else {
        @extern(*const fn (?*anyopaque, c_int) callconv(.c) void, .{ .name = "func_800DD918" })(s, 0);
        planFn("func_800DC3CC")(s);
    }

    if (s.count >= 2) {
        var pass: usize = 0;
        while (pass < 3) : (pass += 1) {
            if (selected[pass] != 0) {
                @extern(*const fn (?*anyopaque, c_int) callconv(.c) void, .{ .name = "func_800DC574" })(s, selected[pass]);
                break;
            }
        }
    }

    var i: c_int = 0;
    while (i < s.count) : (i += 1) {
        if (i > 0 and @as(c_int, psx.getBits(entryAt(s, i), "threshold")) -% @as(c_int, psx.getBits(entryAt(s, 0), "threshold")) < 80) {
            psx.setBits(entryAt(s, i), "threshold", psx.conv(u16, @as(c_int, psx.getBits(entryAt(s, 0), "threshold")) +% 80));
        }
        const e = entryAt(s, i);
        psx.setBits(e, "threshold", psx.conv(u16, @as(c_int, psx.getBits(e, "threshold")) +% (c.rand() & 7)));
    }

    _ = @extern(*const fn (?*anyopaque) callconv(.c) c_int, .{ .name = "func_800D85D8" })(s);
    if (s.count == 0) {
        s.mode = 7;
    } else {
        planFn("func_800D9DD8")(s);
    }

    if (v.g.D_800F5920().* != null) {
        const self = psx.get(s.actor).?;
        if (c.vs_battle_getStateFlag(0xA6) == 4 and self.unk9 == 24 and self.unkC == 4) {
            c.vs_battle_setStateFlag(0xA6, 5);
        }
        if (v.g.D_800F5920().*.?.unk16 != 0 and @as(c_uint, @bitCast(@as(c_int, self.subType) -% 172)) < 2) {
            v.g.D_800F5920().*.?.unk17 = psx.conv(u8, s.count);
            s.count = 0;
        }
    }
}
