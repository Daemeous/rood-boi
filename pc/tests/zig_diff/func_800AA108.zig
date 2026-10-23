//! zig_diff spec: func_800AA108 -> func_800AA108.func_800AA108 (40564.c).
//! Inputs: an index 0..16 into D_800F4538 whose entry is null or an arena
//! D_800F4538_t (random bytes, facing at the edges, unk5C0 and unk5C4 random),
//! a valid func_8006EBF8_t_fields (its unk0_24 byte random, the C always
//! dereferences it), and arg2/arg3 that are zero or at the edges of c_int,
//! negatives included. The callees are recording stubs that write what the
//! real ones can change: func_800AA290 the actor's facing and arg1's unk0_24,
//! func_800AA984 the actor's unk5C4, func_800AA490 arg1's unk0_0.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AA108));

pub const classes = [_][:0]const u8{ "null-actor", "both-zero", "arg2-nonzero", "arg3-nonzero" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const idx: c_int = ctx.range(c_int, 0, 16);
    const table = v.g.D_800F4538();
    const arg1 = ctx.alloc(v.func_8006EBF8_t_fields);
    const actor: ?*v.D_800F4538_t = if (ctx.chance(6)) null else ctx.alloc(v.D_800F4538_t);
    table[@intCast(idx)] = actor;
    if (actor) |a| {
        a.unk0.facing = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
        a.unk5C0 = ctx.pick(u16, &.{ 0, 1, 0xFFFF, 0x8000 });
        a.unk5C4 = ctx.int(u16);
    }
    const arg2: c_int = if (ctx.chance(3)) 0 else ctx.pick(c_int, &.{ 1, -1, 2147483647, -2147483648 });
    const arg3: c_int = if (ctx.chance(2)) 0 else ctx.pick(c_int, &.{ 1, -1, 2147483647, -2147483648 });
    return .{ idx, arg1, arg2, arg3 };
}

pub fn classify(args: Args) usize {
    if (v.g.D_800F4538()[@intCast(args[0])] == null) return 0;
    if (args[2] != 0) return 2;
    if (args[3] != 0) return 3;
    return 1;
}

pub const stubs = struct {
    pub fn func_800AA290(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype, arg3: anytype) void {
        _ = arg2;
        _ = arg3;
        const tbl = v.g.D_800F4538();
        if (tbl[@intCast(arg0)]) |a| a.unk0.facing = ctx.value(i16);
        const p: ?*v.func_8006EBF8_t_fields = @ptrCast(@alignCast(arg1));
        if (p) |q| q._bits_0[3] = ctx.value(u8);
    }

    pub fn func_800AA984(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype) void {
        _ = arg1;
        _ = arg2;
        const tbl = v.g.D_800F4538();
        if (tbl[@intCast(arg0)]) |a| a.unk5C4 = ctx.value(u16);
    }

    pub fn func_800AA490(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype, arg3: anytype) void {
        _ = arg0;
        _ = arg2;
        _ = arg3;
        const p: ?*v.func_8006EBF8_t_fields = @ptrCast(@alignCast(arg1));
        if (p) |q| q._bits_0[0] = ctx.value(u8);
    }
};
