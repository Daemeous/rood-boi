//! zig_diff spec: func_800DAAD8 -> func_800DAAD8.func_800DAAD8 (71618.c).
//! Inputs: a da4bcState in the arena whose pending flag is zero three times in
//! four (otherwise 0x80 or 0xFF), a target16 from -128..127 with the 16 edge
//! and the 0 and -1 negatives, mode and action at their edges (mode 1 and
//! action 7 both common), an entry in the arena with random flagsC, the 16
//! targets with random mode, category (with and without its top bit) and
//! oldMode, and a chain of 0..4 actors hung off vs_battle_actors[0] with ids
//! -20..40 (out-of-range ids index targets inside the state, as the C allows).
//! The six callees are recording stubs that return zero and nonzero, and that
//! rewrite the fields the C reads back: mode and action (after the DB370 call),
//! the entry's flagsC (after the DA4BC call), and every target's oldMode (after
//! the DB61C call).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_71618");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DAAD8));

pub const classes = [_][:0]const u8{
    "pending",
    "target-16-up-no-actors",
    "target-16-up-actors",
    "target-below-16",
};

fn targetsOf(state: *v.da4bcState) *v.da4bcTargetState {
    return @ptrCast(@alignCast(state));
}

fn stateOf(p: anytype) *v.da4bcState {
    const T = @TypeOf(p);
    if (@typeInfo(T) == .optional) return @ptrCast(@alignCast(p.?));
    return @ptrCast(@alignCast(p));
}

pub const stubs = struct {
    pub fn func_800DA4BC(ctx: *zd.Ctx, state: anytype, entry: anytype) c_int {
        _ = state;
        const e: *v.da4bcEntry = @ptrCast(@alignCast(entry));
        if (ctx.value(u1) == 1) e.flagsC = ctx.value(u32);
        if (ctx.value(u1) == 0) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn func_800DB370(ctx: *zd.Ctx, state: anytype, target: anytype, a: anytype, b: anytype) c_int {
        _ = target;
        _ = a;
        _ = b;
        const s = stateOf(state);
        if (ctx.value(u1) == 1) s.mode = ctx.value(u8);
        if (ctx.value(u1) == 1) s.action = ctx.value(u8);
        if (ctx.value(u1) == 0) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn func_800DB74C(ctx: *zd.Ctx, state: anytype, target: anytype) c_int {
        _ = state;
        _ = target;
        if (ctx.value(u1) == 0) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn func_800DB61C(ctx: *zd.Ctx, state: anytype, k: anytype) c_int {
        _ = k;
        const ts = targetsOf(stateOf(state));
        if (ctx.value(u1) == 1) {
            for (0..16) |i| psx.setBits(&ts.targets[i], "oldMode", ctx.value(u2));
        }
        if (ctx.value(u1) == 0) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn func_800E678C(ctx: *zd.Ctx, p: anytype) void {
        if (ctx.value(u1) == 1) stateOf(p).action = ctx.value(u8);
    }

    pub fn func_800E2CCC(ctx: *zd.Ctx, p: anytype) void {
        _ = ctx;
        _ = p;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.da4bcState);
    const entry = ctx.alloc(v.da4bcEntry);
    psx.put(&state.entries[0], entry);

    state.pending = if (ctx.chance(4)) ctx.pick(u8, &.{ 1, 0x80, 0xFF }) else 0;
    psx.setBits(state, "target16", ctx.pick(i8, &.{ 0, 15, 16, 17, -1, -128, 127 }));
    state.mode = ctx.pick(u8, &.{ 1, 1, 0, 2, 7 });
    state.action = ctx.pick(u8, &.{ 6, 7, 8 });

    const ts = targetsOf(state);
    for (0..16) |i| {
        psx.setBits(&ts.targets[i], "mode", ctx.pick(u2, &.{ 0, 1, 2, 3 }));
        psx.setBits(&ts.targets[i], "category", ctx.pick(u7, &.{ 0, 1, 64, 127 }));
        psx.setBits(&ts.targets[i], "oldMode", ctx.pick(u2, &.{ 0, 1, 2, 3 }));
    }

    var head: ?*v.vs_battle_actor = null;
    const count: usize = if (ctx.chance(5)) 0 else ctx.range(usize, 1, 4);
    for (0..count) |_| {
        const a = ctx.alloc(v.vs_battle_actor);
        a.id = ctx.range(i32, -20, 40);
        psx.put(&a.next, head);
        head = a;
    }
    v.g.vs_battle_actors()[0] = head;

    return .{state};
}

pub fn classify(args: Args) usize {
    const s = args[0].?;
    if (s.pending != 0) return 0;
    if (psx.getBits(s, "target16") < 16) return 3;
    return if (v.g.vs_battle_actors()[0] == null) 1 else 2;
}
