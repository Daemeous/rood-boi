//! zig_diff spec: func_800D7CFC -> func_800D7CFC.func_800D7CFC (6F014.c).
//! Inputs: the load state D_800F5680 (0 enters the entry block, 1 and 2 are
//! the two cases; the entry stub may rewrite it to 0..2), the load index
//! 0..7, the load table (unk0 zero, negative, or equal to index + 1; each
//! isClut either 0 with slot 0..7, so slot + 24 stays a legal shift, or
//! nonzero with any slot), the heap buffer, and the CD queue slot D_800F568C
//! (null, or an arena block that is zero except its first byte, a state
//! 0..7, so the C's "loaded" test can succeed and the upload path runs).
//! Stubs only draw ctx.value.
//! Not reachable by the inputs: the switch's default edge (state 3 and up)
//! returns an uninitialised var_s0 in the C, so the state is kept in 0..2.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D7CFC));

pub const classes = [_][:0]const u8{
    "state-1",
    "state-2",
    "state-0-unk0-nonzero",
    "state-0-unk0-zero",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const g = v.g;
    g.D_800F5680().* = if (ctx.chance(2)) 0 else ctx.range(u32, 1, 2);
    g.D_800F5630().* = ctx.range(i32, 0, 7);

    const sc = g.D_800F5638();
    sc.unk0 = ctx.pick(i32, &.{ 0, 1, -1, 2, 8, 0x7FFF_FFFF, std.math.minInt(i32) });
    if (ctx.chance(3)) sc.unk0 = g.D_800F5630().* + 1;

    const pool = [_]u16{ 1, 0x8000, 0xFFFF };
    for (0..8) |i| {
        if (ctx.chance(2)) {
            // isClut 0: the C shifts by slot + 24, so slot must stay 0..7.
            sc.isClut[i] = 0;
            sc.slots[i] = ctx.range(u16, 0, 7);
        } else {
            sc.isClut[i] = pool[ctx.range(usize, 0, 2)];
            sc.slots[i] = ctx.pick(u16, &.{ 0, 7, 8, 255, 0x8000, 0xFFFF });
        }
    }

    const heap: ?[*]u32 = if (ctx.chance(2)) null else @ptrCast(@alignCast(ctx.allocBytes(64, 16)));
    g.D_800F56A0().* = heap;

    // D_800F568C: a CD queue slot whose first byte is a small state (zero
    // elsewhere), or null. _loadfile reads the state of a non-null slot.
    const q = psx.global(?*anyopaque, "D_800F568C");
    if (ctx.chance(3)) {
        q.* = null;
    } else {
        const blk = ctx.alloc([64]u8);
        @memset(blk, 0);
        blk[0] = ctx.range(u8, 0, 7);
        q.* = blk;
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const state = v.g.D_800F5680().*;
    if (state == 1) return 0;
    if (state == 2) return 1;
    return if (v.g.D_800F5638().unk0 != 0) 2 else 3;
}

pub const stubs = struct {
    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        _ = size;
        if (ctx.value(u8) < 32) return null;
        return @ptrCast(ctx.alloc([64]u8));
    }

    pub fn vs_main_freeHeapR(ctx: *zd.Ctx, p: anytype) void {
        _ = ctx;
        _ = p;
    }

    pub fn vs_main_allocateCdQueueSlot(ctx: *zd.Ctx, p: anytype) ?*anyopaque {
        _ = p;
        if (ctx.value(u8) < 16) return null;
        const q = ctx.alloc([64]u8);
        @memset(q, 0);
        q[0] = ctx.value(u8) & 7;
        return @ptrCast(q);
    }

    pub fn vs_main_cdEnqueue(ctx: *zd.Ctx, q: anytype, buf: anytype) void {
        _ = ctx;
        _ = q;
        _ = buf;
    }

    pub fn vs_main_freeCdQueueSlot(ctx: *zd.Ctx, q: anytype) void {
        _ = ctx;
        _ = q;
    }

    // The real callee sets the load state the C re-reads after the call.
    pub fn func_800D8038(ctx: *zd.Ctx, n: anytype) void {
        _ = n;
        if (ctx.value(u8) < 128) v.g.D_800F5680().* = ctx.value(u32) % 3;
    }

    pub fn func_8007DFF0(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        return ctx.value(c_int);
    }

    pub fn LoadImage(ctx: *zd.Ctx, rect: anytype, p: anytype) c_int {
        _ = rect;
        _ = p;
        return ctx.value(c_int);
    }
};
