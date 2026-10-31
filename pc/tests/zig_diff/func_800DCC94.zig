//! zig_diff spec: func_800DCC94 -> func_800DCC94.func_800DCC94 (74088.c).
//! Inputs: a state with random targets (all four classifications), a
//! special flag that is sometimes set, a candidate, an optional metadata block
//! whose flags cover bits 5 and 4 in every combination, and a mode of 0, 1,
//! -1, 2 or INT_MIN. Up to three actors in the global list, ids 0-15. The
//! 16-entry actionScoreState table (D_800F5878) is set in full, with
//! maxActionCost zero and nonzero. The scratchpad stat rows are written by the
//! func_800DC424 stub: maxHP and hp edges (zero, and the exact hp<<7 == maxHP*44
//! point), limit10/limit14 zero and nonzero. rand() returns 115 a quarter of
//! the time, so the early-return compare sees equal sides often. The callees
//! rewrite the candidate's score and weight and the state's targets, which
//! the C re-reads.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_74088");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DCC94));

pub const classes = [_][:0]const u8{ "no-action", "flag32", "special", "mode", "plain" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.actionScoreState);
    const candidate = ctx.alloc(v.actionCandidateEntry);

    const action: ?*v.actionScoreMetadata = if (ctx.chance(8)) null else blk: {
        const m = ctx.alloc(v.actionScoreMetadata);
        m.flags = ctx.int(u16);
        break :blk m;
    };

    const mode: c_int = ctx.pick(c_int, &.{ 0, 1, -1, 2, std.math.minInt(c_int) });

    state.special = if (ctx.chance(3)) 0 else ctx.int(u8);
    for (&state.targets) |*t| t.classification = ctx.int(u8);

    // The per-row table the C indexes with row->unknown & 15.
    const table = psx.globalMany(?*v.actionScoreState, "D_800F5878");
    for (0..16) |i| {
        const s = ctx.alloc(v.actionScoreState);
        s.maxActionCost = if (ctx.chance(2)) 0 else ctx.int(u8);
        table[i] = s;
    }

    // The global actor list: 0-3 actors, each with an id 0-15.
    const count = ctx.range(u8, 0, 3);
    var head: ?*v.vs_battle_actor = null;
    var i: u8 = 0;
    while (i < count) : (i += 1) {
        const a = ctx.alloc(v.vs_battle_actor);
        a.id = ctx.range(i32, 0, 15);
        psx.put(&a.next, head);
        head = a;
    }
    v.g.vs_battle_actors()[0] = head;

    return .{ state, candidate, mode, action };
}

pub fn classify(args: Args) usize {
    const action = args[3] orelse return 0;
    const flags = action.flags;
    if ((flags & 32) != 0) return 1;
    if (args[0].?.special != 0 and (flags & 16) != 0) return 2;
    if (args[2] != 0) return 3;
    return 4;
}

pub const stubs = struct {
    pub fn rand(ctx: *zd.Ctx) c_int {
        if (ctx.value(u2) == 0) return 115;
        return ctx.value(u15);
    }

    pub fn func_800DC424(ctx: *zd.Ctx, stats: anytype) void {
        _ = stats;
        const rows: [*]v.actionStatSnapshot = @ptrFromInt(zd.scratchpad + 0x18C);
        for (0..16) |i| {
            const r = &rows[i];
            r.unknown = ctx.value(i32);
            r.status = ctx.value(u32);
            r.mp = ctx.value(i16);
            r.maxMP = ctx.value(i16);
            if (ctx.value(u8) < 64) r.maxMP = 0;
            const pick = ctx.value(u8);
            if (pick < 40) {
                r.hp = 0;
                r.maxHP = 0;
            } else if (pick < 80) {
                const k = ctx.value(u3);
                r.maxHP = 32 * @as(i16, k);
                r.hp = 11 * @as(i16, k);
            } else {
                r.hp = ctx.value(i16);
                r.maxHP = ctx.value(i16);
                if (ctx.value(u8) < 64) r.maxHP = 0;
            }
            r.value10 = ctx.value(i16);
            r.limit10 = ctx.value(i16);
            if (ctx.value(u8) < 64) r.limit10 = 0;
            r.value14 = ctx.value(i16);
            r.limit14 = ctx.value(i16);
            if (ctx.value(u8) < 64) r.limit14 = 0;
        }
    }

    pub fn func_800DCAA0(ctx: *zd.Ctx, state: anytype, addr: anytype, cand: anytype, mode: anytype) void {
        _ = addr;
        _ = mode;
        const st: *v.actionScoreState = @ptrCast(@alignCast(state));
        const cd: *v.actionCandidateEntry = @ptrCast(@alignCast(cand));
        cd.score = ctx.value(i16);
        if (ctx.value(u1) == 1) psx.setBits(cd, "weight", ctx.value(u8));
        st.targets[ctx.value(u4)].classification = ctx.value(u8);
    }

    pub fn func_800DCBD8(ctx: *zd.Ctx, cand: anytype) void {
        const cd: *v.actionCandidateEntry = @ptrCast(@alignCast(cand));
        if (ctx.value(u2) == 0) cd.score = ctx.value(i16);
        if (ctx.value(u2) == 0) psx.setBits(cd, "weight", ctx.value(u8));
    }

    pub fn func_800DC284(ctx: *zd.Ctx, a: anytype, b: anytype, c_: anytype, d: anytype) c_int {
        _ = a;
        _ = b;
        _ = c_;
        _ = d;
        return ctx.value(i16);
    }
};
