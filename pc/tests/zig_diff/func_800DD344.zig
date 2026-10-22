//! zig_diff spec: func_800DD344 -> func_800DD344.func_800DD344 (74088.c).
//! Inputs: an actionReactionState in the arena (flag13 nonzero about a third
//! of the time, flag21 from {0, 1, 0x80, 0xFF} or random, id 0-15, target
//! flags biased to classifications 0-3), 0-4 actor nodes on vs_battle_actors[0]
//! (an empty list sometimes) with ids 0-15 (0 and the state's own id included),
//! and all 18 D_800F5878 objects with flag2C of 3, 2 or 0, flag13 0 or 1, and
//! target flags biased so the classification, the raw & 1 test and the ally
//! scan's breaks and complete runs are all reached. Random arena bytes supply
//! the counter, previous and high raw bits. func_800DC784 is a stub that
//! rewrites a target's flags or raw, so the C's later reads see its effect.
//! The stub takes the state's address as an integer and writes through byte
//! offsets (targets at 420, four bytes per target), so it never needs the
//! layout of the C-side target type.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_74088");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DD344));

/// Byte offset of `targets` in actionReactionState (see views_74088).
const targets_off: usize = 420;

pub const classes = [_][:0]const u8{ "empty", "flag13", "flag21", "neither" };

pub const stubs = struct {
    pub fn func_800DC784(ctx: *zd.Ctx, state: anytype, id: anytype) void {
        const base: [*]u8 = @ptrFromInt(@intFromPtr(state));
        const i: usize = @as(usize, @intCast(id)) & 15;
        base[targets_off + 4 * i] = ctx.value(u8);
        if (ctx.value(u1) == 1) {
            const j: usize = (i + 1) & 15;
            std.mem.writeInt(u32, base[targets_off + 4 * j ..][0..4], ctx.value(u32), .little);
        }
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.actionReactionState);
    state.id = ctx.range(u8, 0, 15);
    state.flag13 = if (ctx.chance(3)) ctx.pick(u8, &.{ 1, 0x80, 0xFF }) else 0;
    state.flag21 = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });
    for (&state.targets) |*t| t.b.flags = ctx.pick(u8, &.{ 0x80, 0xC0, 0x40, 0x00 });

    const table = v.g.D_800F5878();
    for (0..18) |i| {
        const o = ctx.alloc(v.func_800DEEA4_t2);
        const r: *v.actionReactionState = @ptrCast(@alignCast(o));
        r.flag2C = ctx.pick(u8, &.{ 3, 2, 0 });
        r.flag13 = ctx.pick(u8, &.{ 0, 1 });
        for (&r.targets) |*t| {
            t.b.flags = ctx.pick(u8, &.{ 0x80, 0x80, 0xC0, 0x40, 0x00 });
            // The ally test's last arm: a running counter with previous class 2 (the
            // arena's random bytes alone left it unreached).
            psx.setBits(&t.p, "counter", ctx.pick(u7, &.{ 0, 1, 127 }));
            psx.setBits(&t.p, "previous", ctx.int(u2));
        }
        table[i] = o;
    }

    var head: ?*v.vs_battle_actor = null;
    const n = ctx.range(u8, 0, 4);
    var k: u8 = 0;
    while (k < n) : (k += 1) {
        const node = ctx.alloc(v.vs_battle_actor);
        node.id = ctx.range(i32, 0, 15);
        psx.put(&node.next, head);
        head = node;
    }
    v.g.vs_battle_actors()[0] = head;
    return .{state};
}

pub fn classify(args: Args) usize {
    const state = args[0].?;
    if (v.g.vs_battle_actors()[0] == null) return 0;
    if (state.flag13 != 0) return 1;
    if (state.flag21 != 0) return 2;
    return 3;
}
