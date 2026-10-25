//! zig_diff spec: func_8009F314 -> func_8009F314.func_8009F314 (30DB0.c).
//! Inputs: an actor index (0-16, or 0-15 when the top actor is null, since
//! the C then indexes the object table) and a random animate value (zero,
//! one, minus one, and the extremes). Three arena actors and two arena
//! objects fill the two pointer tables; every table entry is null or one of
//! them. The top actor's flags word at +8 takes four shapes (the first
//! branch not taken, taken through the bits 16-18/21 mask, taken through
//! mode 3, taken through other nonzero modes), its word at +0xC takes zero,
//! nonzero and sign-bit nibbles, unkF is zero about half the time, and vy is
//! zero, positive or negative. unk17FD is 0/1, or 2..16 for a recursion
//! whose target is never the top actor and never an empty index 16.
//! Stubs: func_8008C49C writes a random word into a table actor's +0xC (the
//! C re-reads it), func_800AD494 returns a null or arena out-parameter and
//! sometimes rewrites the flags word, func_8009FD38 sets unk17FD to 0 or 1.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009F314));

pub const classes = [_][:0]const u8{
    "no-actor-no-object",
    "no-actor-object",
    "actor-flags-not-taken",
    "actor-flags-taken-nibble",
    "actor-flags-taken-no-nibble",
    "actor-recurse",
};

fn actorOf(p: anytype) *v.D_800F4538_t {
    const opt: ?*anyopaque = p;
    return @ptrCast(@alignCast(opt.?));
}

pub const stubs = struct {
    pub fn func_8008C49C(ctx: *zd.Ctx, a: c_int, b: c_int) void {
        _ = a;
        _ = b;
        if (ctx.value(u1) == 1) return;
        const t = v.g.D_800F4538();
        if (t[ctx.value(u8) % 17]) |p| psx.byteAt(u32, p, 0xC).* = ctx.value(u32);
    }

    pub fn func_8009FD38(ctx: *zd.Ctx, p: anytype) void {
        if (ctx.value(u1) == 1) return;
        actorOf(p).unk17FD = ctx.value(u8) & 1;
    }

    pub fn func_800AD494(ctx: *zd.Ctx, p: anytype, id: anytype, out: anytype) c_int {
        _ = id;
        if (ctx.value(u8) < 128) {
            out[0] = null;
        } else {
            out[0] = @ptrCast(@alignCast(ctx.allocBytes(16, 2)));
        }
        if (ctx.value(u1) == 1) psx.byteAt(u32, actorOf(p), 8).* = ctx.value(u32);
        return ctx.value(c_int);
    }
};

fn makeFlags(ctx: *zd.Ctx) u32 {
    var f: u32 = ctx.int(u32);
    switch (ctx.range(u8, 0, 3)) {
        0 => {
            f &= ~@as(u32, 0x270000);
        },
        1 => {
            f = (f & ~@as(u32, 0x270000)) | ctx.edge(u32, &.{ 0x10000, 0x20000, 0x40000, 0x200000 });
        },
        2 => {
            f = (f & ~@as(u32, 0x70000)) | 0x30000;
        },
        else => {
            f = (f & ~@as(u32, 0x270000)) | (ctx.edge(u32, &.{ 1, 2, 4, 5, 6, 7 }) << 16);
        },
    }
    return f;
}

fn flagsTaken(f: u32) bool {
    return (f & 0x270000) != 0 or (f & 0x180000) == 0x100000;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const dt = v.g.D_800F4538();
    const ot = v.g.D_800F45E0();
    const acts = [3]*v.D_800F4538_t{ ctx.alloc(v.D_800F4538_t), ctx.alloc(v.D_800F4538_t), ctx.alloc(v.D_800F4538_t) };
    const objs = [2]*v.D_800F45E0_t{ ctx.alloc(v.D_800F45E0_t), ctx.alloc(v.D_800F45E0_t) };

    for (acts) |a| {
        psx.byteAt(u32, a, 8).* = makeFlags(ctx);
        psx.byteAt(u32, a, 0xC).* = ctx.pick(u32, &.{ 0, 0x10, 0x20, 0x8000_0000, 0x8000_0001, 1 });
        if (ctx.chance(2)) a.unk0.unkF = 0;
        a.unk0.unk34.vy = ctx.pick(i16, &.{ 0, 0, 1, -1, 32767, -32768 });
        a.unk17FD = ctx.range(u8, 0, 1);
    }

    for (ot) |*e| {
        const o: ?*v.D_800F45E0_t = if (ctx.chance(2)) null else objs[ctx.range(usize, 0, 1)];
        e.* = o;
    }
    for (dt) |*e| {
        const choice: ?*v.D_800F4538_t = switch (ctx.range(u8, 0, 3)) {
            1 => acts[1],
            2 => acts[2],
            else => null,
        };
        e.* = choice;
    }

    const topNull = ctx.chance(5);
    const actorId: c_int = if (topNull) ctx.range(c_int, 0, 15) else ctx.range(c_int, 0, 16);
    const id: usize = @intCast(actorId);
    const top = acts[0];
    dt[id] = if (topNull) null else top;

    if (ctx.chance(2)) {
        var k = ctx.range(u8, 2, 16);
        if (@as(usize, k) == id) k = if (k == 16) 2 else k + 1;
        top.unk17FD = k;
        if (k == 16 and dt[16] == null) dt[16] = acts[1];
    }

    const animate = ctx.pick(c_int, &.{ 0, 0, 1, -1, 0x7fff_ffff, -0x8000_0000 });
    return .{ actorId, null, animate };
}

pub fn classify(args: Args) usize {
    const id: usize = @intCast(args[0]);
    const dt = v.g.D_800F4538();
    const a = dt[id] orelse return if (v.g.D_800F45E0()[id] != null) 1 else 0;
    if (a.unk17FD >= 2) return 5;
    const f = psx.byteAt(u32, a, 8).*;
    if (!flagsTaken(f)) return 2;
    return if ((psx.byteAt(u32, a, 0xC).* & 0xF) != 0) 3 else 4;
}
