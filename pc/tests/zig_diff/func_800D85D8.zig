//! zig_diff spec: func_800D85D8 -> func_800D85D8.func_800D85D8 (6FC00.c).
//! Inputs: a candidate state in the arena with 16 valid entries (weights
//! 0/1/0x7F/0x80/0xFF, thresholds equal to, one to three above or below the
//! global unk18, or far from it), a count from -2 to 16 (the C never reads past
//! entries[15]), entries[0] live or dead (often live with threshold exactly
//! unk18+31, the case that separates "+30 <" from "+31 <"), a D_800F58BC global
//! with edge unk18 values, and a chain of 0-3 actors on vs_battle_actors[0] (or
//! none) with ids 0-15 and target flags that match or miss the `>> 6 == 3`
//! test. The three callees are stubs that answer zero and nonzero, rewrite the
//! entry weight, unk18, the count, the head entry, and the target flags, so the
//! C reads back what they change.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_6FC00");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D85D8));

pub const classes = [_][:0]const u8{ "no-entries", "first-dead", "first-live" };

/// Spare entries the stubs splice into the state (allocated by prepare).
var spares: [4]*v.actionCandidateEntry = undefined;

fn entryOf(p: anytype) *v.actionCandidateEntry {
    return @ptrCast(@alignCast(p));
}

fn stateOf(p: anytype) *v.actionCandidateState {
    return @ptrCast(@alignCast(p));
}

fn thresholdFor(ctx: *zd.Ctx, unk18: u16) u16 {
    if (ctx.chance(4)) return unk18;
    if (ctx.chance(3)) return unk18 +% 31;
    if (ctx.chance(3)) return unk18 +% 30;
    if (ctx.chance(4)) return unk18 +% 29;
    return ctx.pick(u16, &.{ 0, 1, 0x7FFF, 0x8000, 0xFFFF });
}

pub const stubs = struct {
    pub fn func_800E4CF4(ctx: *zd.Ctx, state: anytype, entry: anytype, target: anytype) c_int {
        _ = state;
        _ = target;
        if (ctx.value(u8) < 16) {
            if (v.g.D_800F58BC().*) |g| g.unk18 = ctx.value(u16);
        }
        if (ctx.value(u8) < 24) {
            const e = entryOf(entry orelse return 0);
            psx.setBits(e, "weight", ctx.value(u8));
        }
        if (ctx.value(u8) < 80) return 0;
        return 1;
    }

    pub fn func_800E4DF8(ctx: *zd.Ctx, state: anytype, row: anytype, column: anytype) c_int {
        _ = row;
        _ = column;
        const s = stateOf(state);
        if (ctx.value(u8) < 32) s.count = @as(i32, ctx.value(u8) % 17);
        if (ctx.value(u8) < 32) psx.put(&s.entries[ctx.value(u8) % 16], spares[ctx.value(u8) % 4]);
        if (ctx.value(u8) < 80) return 0;
        return 1;
    }

    pub fn func_800D9DD8(ctx: *zd.Ctx, state: anytype) void {
        const s = stateOf(state);
        if (ctx.value(u8) < 96) psx.put(&s.entries[0], spares[ctx.value(u8) % 4]);
        if (ctx.value(u8) < 96) s.count = @as(i32, ctx.value(u8) % 17);
        if (ctx.value(u8) < 96) s.targets[ctx.value(u8) % 16].flags = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.actionCandidateState);

    const dat = ctx.alloc(v.D_800F58BC_t);
    dat.unk18 = ctx.pick(u16, &.{ 0, 1, 29, 30, 0x7FFF, 0x8000, 0xFFFE, 0xFFFF });
    v.g.D_800F58BC().* = dat;

    for (0..16) |i| {
        const e = ctx.alloc(v.actionCandidateEntry);
        psx.setBits(e, "weight", ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF }));
        psx.setBits(e, "threshold", thresholdFor(ctx, dat.unk18));
        psx.setBits(e, "targetId", ctx.int(u4));
        psx.setBits(e, "row", ctx.int(u3));
        psx.setBits(e, "column", ctx.int(u2));
        // Entry 0 often live with threshold exactly unk18+31: the else-branch
        // then sees unk18+30 < threshold as true, and "+31 <" as false.
        if (i == 0 and ctx.chance(2)) {
            psx.setBits(e, "threshold", dat.unk18 +% 31);
            psx.setBits(e, "weight", ctx.pick(u8, &.{ 1, 0x7F, 0x80, 0xFF }));
        }
        psx.put(&state.entries[i], e);
    }
    for (&spares) |*sp| sp.* = ctx.alloc(v.actionCandidateEntry);

    var head: ?*v.vs_battle_actor = null;
    const n = ctx.range(u8, 0, 3);
    for (0..n) |_| {
        const a = ctx.alloc(v.vs_battle_actor);
        a.id = ctx.range(i32, 0, 15);
        psx.put(&a.next, head);
        head = a;
    }
    v.g.vs_battle_actors()[0] = if (ctx.chance(5)) null else head;

    for (&state.targets) |*t| {
        t.flags = ctx.pick(u8, &.{ 0x00, 0x3F, 0x40, 0x80, 0xC0, 0xFF });
    }

    state.count = ctx.range(i32, -2, 16);
    state.status = ctx.int(u8);

    return .{state};
}

pub fn classify(args: Args) usize {
    const s = args[0].?;
    if (s.count <= 0) return 0;
    const e0 = psx.get(s.entries[0]).?;
    return if (psx.getBits(e0, "weight") == 0) 1 else 2;
}
