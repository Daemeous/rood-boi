//! zig_diff spec: func_800A1108 -> func_800A1108.func_800A1108 (30DB0.c).
//! Inputs: arg0 is an index into the two pointer tables (0..15, and 16 only
//! where the first table has an entry there). The first table's entry at
//! arg0 is set (the first branch), or null with the second table's entry set
//! (the fallback), or both null (returns -1). Decoy entries elsewhere are
//! random null/objA/objB, so a mutated index reads a different object.
//! arg1 is a 16-byte arena block, a scratchpad address, or an address inside
//! one of the actors (the stores then alias the fields the C reads).
//! No callees: nothing to stub.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A1108));

pub const classes = [_][:0]const u8{ "first-table", "fallback", "not-found" };

fn pickObj(ctx: *zd.Ctx, a: *v.D_800F4538_t, b: *v.D_800F4538_t) ?*v.D_800F4538_t {
    return switch (ctx.random.uintLessThan(u8, 3)) {
        0 => null,
        1 => a,
        else => b,
    };
}

fn asT2(p: *v.D_800F4538_t) *v.D_800F45E0_t {
    return @ptrCast(@alignCast(p));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const objA = ctx.alloc(v.D_800F4538_t);
    const objB = ctx.alloc(v.D_800F4538_t);
    const t1 = v.g.D_800F4538();
    const t2 = v.g.D_800F45E0();

    for (t1) |*e| e.* = pickObj(ctx, objA, objB);
    for (t2) |*e| e.* = if (pickObj(ctx, objA, objB)) |p| asT2(p) else null;

    const arg0: c_int = ctx.edge(c_int, &.{ 0, 1, 5, 15, 16 });
    const idx: usize = @intCast(arg0);

    // Index 16 exists only in the first table: keep it on the first branch.
    const mode: u8 = if (arg0 == 16) 0 else ctx.random.uintLessThan(u8, 3);
    switch (mode) {
        0 => t1[idx] = objA,
        1 => {
            t1[idx] = null;
            t2[idx] = asT2(objB);
        },
        else => {
            t1[idx] = null;
            t2[idx] = null;
        },
    }

    const where = ctx.random.uintLessThan(u8, 4);
    const arg1: ?*anyopaque = switch (where) {
        0 => @ptrFromInt(zd.scratchpad + ctx.range(usize, 0, 0xF00)),
        1 => blk: {
            const base: [*]u8 = @ptrCast(objA);
            break :blk @ptrCast(base + ctx.range(usize, 0, @sizeOf(v.D_800F4538_t) - 16));
        },
        2 => blk: {
            const base: [*]u8 = @ptrCast(objB);
            break :blk @ptrCast(base + ctx.range(usize, 0, @sizeOf(v.D_800F4538_t) - 16));
        },
        else => blk: {
            const buf: *anyopaque = @ptrCast(ctx.allocBytes(16, 4));
            break :blk buf;
        },
    };

    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    const idx: usize = @intCast(args[0]);
    if (v.g.D_800F4538()[idx] != null) return 0;
    if (v.g.D_800F45E0()[idx] != null) return 1;
    return 2;
}
