//! zig_diff spec: func_800A9D90 -> func_800A9D90.func_800A9D90 (40564.c).
//! Inputs: an actor index 0-16 whose table slot is empty (null, with or
//! without a null arg1, which the C never reads) or points at a shared arena
//! object. arg1 is an arena SVECTOR with edge values so the position
//! differences wrap i16. arg2 is the -1 sentinel (the early-return path) or
//! other values: -2, 0, 1, 0xFFFF, the c_int extremes and random. The callee
//! stub copies the vector it receives (the C's sp10) into the actor's
//! unk5C6..unk5CA fields, so a port that computes a wrong component, moves
//! or drops the unk5C2 store or the restore is caught by the state hash at
//! the call and at return. The stub also sometimes writes the actor's
//! position and unk5C2 itself, as the real callee may.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A9D90));

pub const classes = [_][:0]const u8{ "empty-slot", "sentinel", "override" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    const idx: c_int = ctx.range(u5, 0, 16);
    const empty = ctx.chance(6);

    const obj = ctx.alloc(v.D_800F4538_t);
    obj.unk0.position.vx = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    obj.unk0.position.vy = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    obj.unk0.position.vz = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });

    for (table, 0..) |*e, i| {
        e.* = if (empty and i == @as(usize, @intCast(idx))) null else if (ctx.chance(4)) null else obj;
    }

    const sv = ctx.alloc(v.SVECTOR);
    sv.vx = ctx.pick(i16, &.{ 0, -1, 32767, -32768 });
    sv.vy = ctx.pick(i16, &.{ 0, -1, 32767, -32768 });
    sv.vz = ctx.pick(i16, &.{ 0, -1, 32767, -32768 });

    const arg1: ?*v.SVECTOR = if (empty and ctx.chance(2)) null else sv;
    const arg2: c_int = if (ctx.chance(3))
        -1
    else
        ctx.pick(c_int, &.{ -2, 0, 1, 0xFFFF, 0x7FFFFFFF, -0x7FFFFFFF - 1 });
    return .{ idx, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const slot = v.g.D_800F4538()[@intCast(args[0])];
    if (slot == null) return 0;
    return if (args[2] == -1) 1 else 2;
}

pub const stubs = struct {
    pub fn func_800AA698(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype) void {
        _ = arg2;
        const sp: *v.SVECTOR = @ptrCast(@alignCast(arg1));
        // The vector the C passes (sp10) is observable through the actor's
        // unk5C6..unk5CA fields, so every component the C computes is compared.
        const slots = v.g.D_800F4538();
        const i: usize = @intCast(arg0);
        if (slots[i]) |a| {
            a.unk5C6 = @bitCast(sp.vx);
            a.unk5C8 = @bitCast(sp.vy);
            a.unk5CA = @bitCast(sp.vz);
            if (ctx.value(u1) == 1) a.unk0.position.vy = ctx.value(i16);
            if (ctx.value(u1) == 1) a.unk5C2 = ctx.value(u16);
        }
        if (ctx.value(u1) == 1) sp.vx = ctx.value(i16);
    }
};
