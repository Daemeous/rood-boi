//! zig_diff spec: func_800E0918 -> func_800E0918.func_800E0918 (771A8.c).
//! Inputs: a movementCheckState in the arena (flags masked by 6 zero or
//! not, a random height and position) whose object (flags with or without
//! 0x200000) is set through psx.put; D_800F58BC always points at a block in
//! the arena, with unk14 zero or equal to, one above, or below the sum
//! (func_800E42E0 + height), so that `<` sees equal sides. mode is 0, 1, -1,
//! INT_MIN or 2, so `if (mode)` sees values with and without the sign bit.
//! The distance from func_800E45F4 is 0, 15, 16, 17 or large, so `distance < 16`
//! sees equal sides. func_800D954C returns 0, 1, -1 or the int extremes.
//! The callbacks func_800E4B2C and func_800E4B70 are variadic in the C, so
//! their stubs get no arguments from the harness; they write through the
//! state pointer prepare recorded.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_771A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E0918));

pub const classes = [_][:0]const u8{
    "inactive",
    "near-unflagged",
    "near-flagged",
    "no-hud-height",
    "out-of-reach",
    "flagged-no-move",
    "flagged-move",
    "unflagged-move",
};

// Per-iteration answers of the stubs (set by prepare, the same in both runs).
var dist45: c_uint = 0;
var e42: c_int = 0;
var d954: c_int = 0;
// The state prepare built, for the variadic callbacks that get no arguments.
var cur_state: ?*v.movementCheckState = null;

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.movementCheckState);
    const object = ctx.alloc(v.movementCheckObject);
    const block = ctx.alloc(v.D_800F58BC_t);
    v.g.D_800F58BC().* = block;
    psx.put(&state.object, object);
    cur_state = state;

    object.flags = ctx.pick(u32, &.{ 0, 0x200000, 0xFFFF_FFFF, 0x8000_0000, 0x1F_FFFF });
    state.flags = ctx.pick(u16, &.{ 0, 0x8000, 0xFFF9, 2, 4, 6, 0xFFFF });
    state.target = ctx.int(i32);
    state.position = ctx.int(i32);
    state.height = ctx.int(i16);

    dist45 = ctx.pick(c_uint, &.{ 0, 15, 16, 17, 0xFFFF_FFFF, 0x8000_0000 });
    e42 = if (ctx.chance(2)) ctx.range(c_int, -1000, 1000) else ctx.range(c_int, -(1 << 29), 1 << 29);
    d954 = ctx.pick(c_int, &.{ 0, 1, -1, std.math.maxInt(c_int), std.math.minInt(c_int) });

    const delta: i64 = if (ctx.chance(2)) 0 else ctx.range(i64, -3, 3);
    const sum: i64 = @as(i64, e42) + state.height;
    block.unk14 = @intCast(sum + delta);
    if (ctx.chance(6)) block.unk14 = 0;

    const mode = ctx.pick(c_int, &.{ 0, 1, -1, std.math.minInt(c_int), 2 });
    const action = ctx.int(c_int);
    return .{ state, action, mode };
}

pub fn classify(args: Args) usize {
    const state = args[0].?;
    const mode = args[2];
    if ((state.flags & 6) == 0) return 0;
    const flagged = (psx.get(state.object).?.flags & 0x200000) != 0;
    if (mode != 0 and dist45 < 16) return if (flagged) 2 else 1;
    const hud = v.g.D_800F58BC().*.?;
    if (hud.unk14 == 0) return 3;
    const sum: i64 = @as(i64, e42) + state.height;
    if (sum >= hud.unk14) return 4;
    if (flagged) return if (d954 > 0) 6 else 5;
    return 7;
}

fn writeState(ctx: *zd.Ctx, s: *v.movementCheckState) void {
    if (ctx.value(u1) == 1) s.flags = ctx.value(u16);
    if (ctx.value(u2) == 0) s.position = ctx.value(i32);
}

fn writeCur(ctx: *zd.Ctx) void {
    if (cur_state) |s| writeState(ctx, s);
}

pub const stubs = struct {
    pub fn func_800E4B2C(ctx: *zd.Ctx, state: anytype, action: anytype) void {
        _ = state;
        _ = action;
        writeCur(ctx);
    }

    pub fn func_800E4B70(ctx: *zd.Ctx, state: anytype, action: anytype) void {
        _ = state;
        _ = action;
        writeCur(ctx);
    }

    pub fn func_800E45F4(ctx: *zd.Ctx, target: anytype, position: anytype) c_uint {
        _ = ctx;
        _ = target;
        _ = position;
        return dist45;
    }

    pub fn func_800E42E0(ctx: *zd.Ctx, position: anytype) c_int {
        _ = ctx;
        _ = position;
        return e42;
    }

    pub fn func_800D954C(ctx: *zd.Ctx, state: anytype, position: anytype) c_int {
        _ = ctx;
        _ = state;
        _ = position;
        return d954;
    }

    pub fn func_800E678C(ctx: *zd.Ctx, state: anytype) void {
        const s: *v.movementCheckState = @ptrCast(@alignCast(state));
        writeState(ctx, s);
    }

    pub fn func_800E4B10(ctx: *zd.Ctx, state: anytype) void {
        const s: *v.movementCheckState = @ptrCast(@alignCast(state));
        writeState(ctx, s);
    }
};
