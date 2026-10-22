//! zig_diff spec: func_800DA4BC -> func_800DA4BC.func_800DA4BC (71618.c).
//! Inputs: a da4bcState and a da4bcEntry in the arena, either of which may be
//! null (the C only reads the state and entry after func_800DB8AC accepts them,
//! so those nulls reach the port only through that stub), flagsC over all 16
//! targets (bit 3 included), a zero or edge distance, radius 0..255, the
//! targets' bit 0 set or clear, and the global D_800F58BC set to a valid object.
//! The equal case (distances[target] == radius): vs_gte_rsqrt is a stub
//! returning a root prepare chose (an edge or random), and a quarter of the
//! runs set distances[target] to exactly the square the C computes from it
//! (one more or one less in others, both sides of the unsigned `<`).
//! The stubs draw only ctx.value: func_800DB8AC returns 0, -1 or 1 (0 for a
//! null argument, which the C never dereferences), func_800D836C and
//! func_800DB370 write state fields the C reads back only through the later
//! calls, and func_800DB370 and func_800DB74C return zero, nonzero, or -1.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_71618");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DA4BC));

pub const classes = [_][:0]const u8{ "null-state", "null-entry", "zero-distance", "flag-set", "flag-clear" };

/// The root vs_gte_rsqrt returns this iteration (chosen by prepare).
var root: c_int = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    // The global D_800F58BC is dereferenced (unk24++) whenever the outer test passes.
    const shared = ctx.alloc(v.D_800F58BC_t);
    shared.unk24 = ctx.range(i32, -1000, 1000);
    v.g.D_800F58BC().* = shared;

    const state = ctx.alloc(v.da4bcState);
    const entry = ctx.alloc(v.da4bcEntry);
    entry.flagsC = ctx.int(u32);
    entry.distance = ctx.pick(u32, &.{ 0, 1, 0xFFFF, 0x10000, 0x7FFF_FFFF, 0x8000_0000, 0xFFFF_FFFF });
    entry.unk0 = ctx.int(u32);
    entry.unk4 = ctx.int(u32);
    state.radius = ctx.pick(u8, &.{ 0, 1, 127, 255 });
    for (&state.distances) |*x| x.* = ctx.pick(u32, &.{ 0, 1, 0x8000_0000, 0xFFFF_FFFF });

    const t: u32 = entry.flagsC & 15;
    if (ctx.chance(4)) entry.distance = 0;

    root = ctx.pick(c_int, &.{ 0, 1, 255, 4096, 0x7FFF, -1, ctx.int(c_int) });
    if (ctx.chance(4)) {
        // Equal case: distances[t] is exactly (as one less or one more) the square the C computes.
        const x: i32 = root +% (@as(i32, state.radius) << 5);
        const rad: u32 = @bitCast(x *% x);
        state.distances[t] = rad +% ctx.pick(u32, &.{ 0, 0, 1, 0xFFFF_FFFF });
    }

    state.targets[t] = if (ctx.chance(2)) (state.targets[t] | 1) else (state.targets[t] & ~@as(u32, 1));

    const s: ?*v.da4bcState = if (ctx.chance(10)) null else state;
    const e: ?*v.da4bcEntry = if (ctx.chance(10)) null else entry;
    return .{ s, e };
}

pub fn classify(args: Args) usize {
    const state = args[0] orelse return 0;
    const entry = args[1] orelse return 1;
    if (entry.distance == 0) return 2;
    return if ((state.targets[entry.flagsC & 15] & 1) != 0) 3 else 4;
}

pub const stubs = struct {
    pub fn vs_gte_rsqrt(ctx: *zd.Ctx, x: anytype) c_int {
        _ = ctx;
        _ = x;
        return root;
    }

    pub fn func_800DB8AC(ctx: *zd.Ctx, state: anytype, entry: anytype) c_int {
        // A null argument is never dereferenced by the C, so the stub refuses it.
        if (state == null or entry == null) return 0;
        return switch (ctx.value(u8) % 4) {
            0 => 0,
            1 => -1,
            else => 1,
        };
    }

    pub fn func_800D836C(ctx: *zd.Ctx, state: anytype) void {
        const s: *v.da4bcState = @ptrCast(@alignCast(state));
        s.targets[ctx.value(u4)] = ctx.value(u32);
    }

    pub fn func_800DB370(ctx: *zd.Ctx, state: anytype, target: c_int, distance: c_int, flag: c_int) c_int {
        _ = target;
        _ = flag;
        _ = distance;
        const s: *v.da4bcState = @ptrCast(@alignCast(state));
        s.distances[ctx.value(u4)] = ctx.value(u32);
        return switch (ctx.value(u8) % 3) {
            0 => 0,
            1 => 1,
            else => -1,
        };
    }

    pub fn func_800DB74C(ctx: *zd.Ctx, state: anytype, target: c_int) c_int {
        _ = state;
        _ = target;
        return switch (ctx.value(u8) % 3) {
            0 => 0,
            1 => 1,
            else => -1,
        };
    }
};
