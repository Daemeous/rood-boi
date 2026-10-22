//! zig_diff spec: func_800DAC80 -> func_800DAC80.func_800DAC80 (71618.c).
//! Inputs: a context and an action in the arena (the action always valid, since
//! the C reads its unkC_0 after the first callees). The 17-entry actor table
//! is filled with two arena actors or null, the whole table null about one run
//! in six. Each actor's word at +8 takes the edge values of the 0x180000 test
//! (zero, one bit, both bits, the sign bit alone, sign bit with the mask bits),
//! so the test sees nonzero with and without the sign bit. D_800F58BC points at
//! an arena object whose byte +8 is the cooldown source. kind is 0 about half
//! the time. The callees return zero in a quarter of their calls (the C's early
//! returns) and write what the C reads back: the action's index (unkC_0, read
//! after func_800DBD80), the context's kind (func_800D836C) and cooldown
//! (func_800DEEFC).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_71618");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DAC80));

pub const classes = [_][:0]const u8{ "table-empty", "kind-zero", "kind-nonzero" };

fn ctxOf(p: anytype) *v.actionSetupContext {
    return @ptrCast(@alignCast(p));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const context = ctx.alloc(v.actionSetupContext);
    const action = ctx.alloc(v.func_800DCAA0_t);
    const actors = [2]*v.D_800F4538_t{ ctx.alloc(v.D_800F4538_t), ctx.alloc(v.D_800F4538_t) };

    for (actors) |a| {
        psx.byteAt(u32, a, 8).* = ctx.pick(u32, &.{ 0, 0x80000, 0x100000, 0x180000, 0x80000000, 0x80180000, 0x7FFFFFFF, 0xFFFFFFFF, 0x00010000 });
    }

    const none = ctx.chance(6);
    for (v.g.D_800F4538()) |*slot| {
        slot.* = if (none or ctx.chance(3)) null else actors[ctx.range(usize, 0, 1)];
    }

    v.g.D_800F58BC().* = ctx.alloc(v.D_800F58BC_t);
    v.g.D_800F58E0().* = ctx.int(i32);

    context.kind = if (ctx.chance(2)) 0 else ctx.int(u8);
    context.cooldown = ctx.int(u8);
    psx.put(&context.action, action);
    psx.setBits(action, "unkC_0", ctx.int(u4));

    return .{ context, action };
}

pub fn classify(args: Args) usize {
    var any = false;
    for (v.g.D_800F4538().*) |p| {
        if (p != null) any = true;
    }
    if (!any) return 0;
    return if (args[0].?.kind == 0) 1 else 2;
}

pub const stubs = struct {
    pub fn func_800DBD80(ctx: *zd.Ctx, context: anytype, action: anytype) c_int {
        _ = context;
        if (ctx.value(u2) == 0) return 0;
        const a: *v.func_800DCAA0_t = @ptrCast(@alignCast(action.?));
        psx.setBits(a, "unkC_0", ctx.value(u4));
        return ctx.value(c_int) | 1;
    }

    pub fn func_800D836C(ctx: *zd.Ctx, context: anytype) void {
        if (ctx.value(u1) == 1) ctxOf(context).kind = ctx.value(u8);
    }

    pub fn func_800D826C(ctx: *zd.Ctx, context: anytype) c_int {
        _ = context;
        if (ctx.value(u2) == 0) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn func_800E45D4(ctx: *zd.Ctx, n: anytype) c_int {
        _ = n;
        return ctx.value(c_int);
    }

    pub fn func_800DEEFC(ctx: *zd.Ctx, context: anytype, n: anytype) void {
        _ = n;
        ctxOf(context).cooldown = ctx.value(u8);
    }

    pub fn func_800DBC60(ctx: *zd.Ctx, context: anytype, action: anytype) c_int {
        _ = context;
        _ = action;
        if (ctx.value(u2) == 0) return 0;
        return ctx.value(c_int) | 1;
    }
};
