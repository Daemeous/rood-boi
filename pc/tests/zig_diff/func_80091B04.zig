//! zig_diff spec: func_80091B04 -> func_80091B04.func_80091B04 (2842C.c).
//! Inputs: two counts (D_800F225A up to the 96-entry table, D_800F225B up to
//! 32 so i+64 stays in range), 96 table entries with flags that are zero or
//! sign-bit set or not, selector edges 0/1/6/7/8/255 for the handler index,
//! and every entry's guest pointers aimed at arena objects. The seven table
//! slots hold two handlers that return a prepared value. The stub for
//! func_80092B04 returns 0, 256 (truncates to 0 in the u8 field) or any
//! c_int, may repoint the entry's unk4 at a spare object, and may change
//! D_800F225B mid-loop. The freeHeap stub scribbles on the block it frees.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80091B04));

pub const classes = [_][:0]const u8{ "empty", "second-only", "first-only", "both" };

var spare: ?*v.D_800F1DD8_t2 = null;
var handler_ret: c_int = 0;

fn handlerA(p: ?*anyopaque) callconv(.c) c_int {
    _ = p;
    return handler_ret;
}

fn handlerB(p: ?*anyopaque) callconv(.c) c_int {
    _ = p;
    return handler_ret ^ 0x55;
}

fn countFirst(ctx: *zd.Ctx) u8 {
    if (ctx.chance(4)) return 0;
    if (ctx.chance(2)) return ctx.edge(u8, &.{ 1, 2, 63, 64, 65, 95, 96 });
    return ctx.range(u8, 1, 96);
}

fn countSecond(ctx: *zd.Ctx) u8 {
    if (ctx.chance(4)) return 0;
    if (ctx.chance(2)) return ctx.edge(u8, &.{ 1, 31, 32 });
    return ctx.range(u8, 1, 32);
}

fn flag(ctx: *zd.Ctx) u8 {
    if (ctx.chance(3)) return 0;
    if (ctx.chance(2)) return ctx.edge(u8, &.{ 1, 127, 128, 255 });
    return ctx.range(u8, 1, 255);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const entries = v.g.D_800F1DD8();
    for (0..96) |i| {
        const e = &entries[i];
        e.unk0 = flag(ctx);
        e.unk1 = ctx.edge(u8, &.{ 0, 1, 6, 7, 8, 255 });
        e.unk2 = flag(ctx);
        e.unk3 = ctx.int(u8);
        psx.put(&e.unk4, ctx.alloc(v.D_800F1DD8_t2));
        psx.put(&e.unk8, ctx.alloc(v.D_800F1DD8_t2));
    }
    spare = ctx.alloc(v.D_800F1DD8_t2);

    for (v.g.D_800E85CC(), 0..) |*slot, k| {
        const h: ?*const anyopaque = if (k % 2 == 0) @ptrCast(&handlerA) else @ptrCast(&handlerB);
        slot.* = h;
    }
    handler_ret = if (ctx.chance(4)) 0 else ctx.pick(c_int, &.{ 256, 255, 128, -1 });

    v.g.D_800F225A().* = countFirst(ctx);
    v.g.D_800F225B().* = countSecond(ctx);
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const a = v.g.D_800F225A().*;
    const b = v.g.D_800F225B().*;
    if (a == 0 and b == 0) return 0;
    if (a == 0) return 1;
    if (b == 0) return 2;
    return 3;
}

pub const stubs = struct {
    pub fn func_80092B04(ctx: *zd.Ctx, a: anytype, b: anytype, e: anytype) c_int {
        _ = a;
        _ = b;
        const entry: *v.D_800F1DD8_t = @ptrCast(@alignCast(e));
        if (ctx.value(u2) == 0) psx.put(&entry.unk4, spare.?);
        if (ctx.value(u3) == 0) v.g.D_800F225B().* = ctx.value(u8) % 33;
        return switch (ctx.value(u2)) {
            0 => 0,
            1 => 256,
            else => ctx.value(c_int),
        };
    }

    pub fn vs_main_freeHeap(ctx: *zd.Ctx, p: ?*anyopaque) void {
        if (ctx.value(u2) == 0) {
            const bytes: [*]u8 = @ptrCast(@alignCast(p));
            bytes[0] = ctx.value(u8);
        }
    }
};
