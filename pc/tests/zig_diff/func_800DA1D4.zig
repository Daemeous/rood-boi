//! zig_diff spec: func_800DA1D4 -> func_800DA1D4.func_800DA1D4 (71618.c).
//! Inputs: a candidate state in the arena with every status (0, 1, 2 and the
//! no-case values 3 and 255), count from -3 to 16 (negative counts and the
//! count of 1 and 2 at the `< 2` and loop tests), all 16 entries valid with
//! thresholds equal to the global's unk18 a third of the time, a shared
//! targetId so the `!= id` test sees both outcomes, target flags on both sides
//! of the flag-3 test, distances at the unsigned edges (ties included), and a
//! linked list of 0-4 battle actors with ids 0-19 (inside the state struct)
//! or none. The callees write the state they can change (targets flags,
//! distances, count) and return zero and nonzero values.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_71618");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DA1D4));

pub const classes = [_][:0]const u8{ "status-0", "status-1", "status-2", "other-status" };

pub const stubs = struct {
    pub fn func_800D82CC(ctx: *zd.Ctx, p: anytype) void {
        const s: *v.actionCandidateState = @ptrCast(@alignCast(p));
        s.targets[ctx.value(u4)].flags = ctx.value(u8);
        s.targetDistancesSquared[ctx.value(u4)] = ctx.value(u32);
    }

    pub fn func_800DB370(ctx: *zd.Ctx, p: anytype, id: anytype, distance: anytype, flag: anytype) c_int {
        _ = id;
        _ = distance;
        _ = flag;
        const s: *v.actionCandidateState = @ptrCast(@alignCast(p));
        if (ctx.value(u2) == 0) s.targets[ctx.value(u4)].flags = ctx.value(u8);
        if (ctx.value(u2) == 0) return 0;
        return ctx.value(c_int);
    }

    pub fn func_800DB74C(ctx: *zd.Ctx, p: anytype, id: anytype) c_int {
        _ = id;
        const s: *v.actionCandidateState = @ptrCast(@alignCast(p));
        if (ctx.value(u2) == 0) s.targets[ctx.value(u4)].flags = ctx.value(u8);
        if (ctx.value(u2) == 0) return 0;
        return ctx.value(c_int);
    }

    pub fn func_800D9E18(ctx: *zd.Ctx, state: anytype, entry: anytype) c_int {
        _ = entry;
        const s: *v.actionCandidateState = @ptrCast(@alignCast(state));
        if (ctx.value(u2) == 0) s.count = ctx.value(u4);
        if (ctx.value(u2) == 0) return 0;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.actionCandidateState);
    state.status = ctx.pick(u8, &.{ 0, 1, 2, 3, 255 });
    state.count = if (ctx.chance(2)) ctx.range(i32, -3, 16) else ctx.range(i32, 0, 2);

    for (&state.targets) |*t| t.flags = ctx.pick(u8, &.{ 0x00, 0x40, 0x80, 0xC0, 0xFF, 0x7F });
    for (&state.targetDistancesSquared) |*d| d.* = ctx.pick(u32, &.{ 0, 1, 0xFFFF_FFFF, 0x8000_0000 });

    // The global the C reads (D_800F58BC->unk18).
    const bg = ctx.alloc(v.D_800F58BC_t);
    bg.unk18 = ctx.pick(u16, &.{ 0, 1, 0xFFFF, 0x8000 });
    v.g.D_800F58BC().* = bg;

    // All 16 entries valid: case 0 reads entries[0] whatever count is.
    const tid = ctx.range(u4, 0, 15);
    for (0..16) |k| {
        const e = ctx.alloc(v.actionCandidateEntry);
        psx.setBits(e, "targetId", if (ctx.chance(2)) tid else ctx.range(u4, 0, 15));
        const thr: u16 = if (ctx.chance(3)) bg.unk18 else ctx.int(u16);
        psx.setBits(e, "threshold", thr);
        e.distanceSquared = ctx.pick(u32, &.{ 0, 1, 0xFFFF_FFFF, 0x8000_0000 });
        psx.put(&state.entries[k], e);
    }

    // Battle actors: an acyclic list of 0-4 with ids inside the state struct.
    const n = ctx.range(u8, 0, 4);
    const nil: ?*v.vs_battle_actor = null;
    var head: ?*v.vs_battle_actor = null;
    var prev: ?*v.vs_battle_actor = null;
    var k: u8 = 0;
    while (k < n) : (k += 1) {
        const a = ctx.alloc(v.vs_battle_actor);
        a.id = ctx.range(i32, 0, 19);
        psx.put(&a.next, nil);
        if (prev) |p| psx.put(&p.next, a) else head = a;
        prev = a;
    }
    v.g.vs_battle_actors()[0] = head;

    return .{state};
}

pub fn classify(args: Args) usize {
    const s = args[0].?;
    return switch (s.status) {
        0 => 0,
        1 => 1,
        2 => 2,
        else => 3,
    };
}
