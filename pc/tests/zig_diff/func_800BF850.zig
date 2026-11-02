//! zig_diff spec: func_800BF850 -> func_800BF850.func_800BF850 (4A0A8.c).
//! Inputs: the battle state byte D_800F4C34 across every case (0..5 and the
//! values past them), the load-selector D_800F4BE0 as a u16 (its (short) cast
//! gives every switch prong, the default and negative values), the load
//! queue pointer (always valid, its first i16 is 4 or not), the room door
//! table index _evtFile (small, negative included, inside a block the harness
//! owns), the actor-slot array D_800F4C38 with null and non-null entries, and
//! the flags that gate the reset and the final branches (zero and nonzero,
//! with the sign bit set). The callees that return values (func_800BFBB8,
//! func_800BD610) return zero and nonzero, and the (u_char) low byte 0xFF with
//! and without high bits; the stubs also rewrite the globals the C reads back.
//! func_8007CEC0, which 4A0A8.c calls with no declaration in scope, is
//! declared through the views' `declarations` (its default stub records it).
//! The thread table D_800F4C38 has 4 entries (threads 0..3).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BF850));

pub const classes = [_][:0]const u8{
    "idle",
    "wait-not-ready",
    "wait-ready",
    "switch",
    "load-done",
    "load-pending",
    "main",
};

fn bytes(ctx: *zd.Ctx) [*]u8 {
    return ctx.allocBytes(64, 16);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const g = v.g;

    // Room door section: the C reads ((short*)section)[_evtFile] for case 2.
    // The block is 256 bytes and the base sits 64 bytes in, so every index
    // in range below stays inside memory the harness owns.
    const block: [*]u8 = ctx.allocBytes(256, 16);
    const base: [*]u8 = block + 64;
    g._mpdRoomDoorSection().* = @as(*anyopaque, @ptrCast(base));
    g._evtFile().* = ctx.range(i16, -8, 40);

    // Pointers the C passes on without dereferencing them.
    g.D_800F4C30().* = if (ctx.chance(4)) null else @as(*anyopaque, @ptrCast(bytes(ctx)));
    g.D_800F4C24().* = if (ctx.chance(4)) null else @as(*anyopaque, @ptrCast(bytes(ctx)));

    // The queue slot the C dereferences in state 3: always valid, first i16 picked.
    const slot: [*]i16 = @ptrCast(@alignCast(bytes(ctx)));
    slot[0] = ctx.pick(i16, &.{ 4, 4, 3, 0, -1 });
    g.D_800F4BBC().* = slot;

    // Actor slots: null or an arena block.
    const arr = g.D_800F4C38();
    for (0..4) |k| {
        arr[k] = if (ctx.chance(3)) null else @as(*u8, @ptrCast(bytes(ctx)));
    }

    g.D_800F4C34().* = ctx.pick(u8, &.{ 0, 1, 2, 3, 4, 5, 0xFF });
    g.D_800F4BB8().* = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });
    g.D_800F4BE0().* = ctx.pick(u16, &.{ 0, 1, 2, 3, 0xFFFF, 0x8000 });
    g.D_800F4BE4().* = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });
    g.D_800F4C6A().* = ctx.int(u8);

    // Gates for the reset and final branches: zero often, nonzero with the sign bit too.
    g.D_800F4C54().* = if (ctx.chance(2)) 0 else ctx.pick(i16, &.{ 1, -1, -32768, 0x4000 });
    g.D_800F4C22().* = ctx.pick(i16, &.{ 0, 0, 1, -1, -32768 });
    g.D_800F4BB6().* = if (ctx.chance(3)) 0 else ctx.pick(i16, &.{ 1, -1, -32768 });
    g.D_800F4BE2().* = if (ctx.chance(3)) 0 else ctx.pick(i16, &.{ 1, -1, -32768 });

    return .{};
}

pub fn classify(_: Args) usize {
    const s = v.g.D_800F4C34().*;
    return switch (s) {
        0 => 0,
        1 => if (v.g.D_800F4BB8().* != 0) 2 else 1,
        2 => 3,
        3 => if (v.g.D_800F4BBC().*.?[0] == 4) 4 else 5,
        else => 6,
    };
}

pub const stubs = struct {
    pub fn func_800BBBE8(ctx: *zd.Ctx) void {
        _ = ctx;
    }

    pub fn func_800BBE94(ctx: *zd.Ctx) void {
        if (ctx.value(u1) == 1) v.g.D_800F4BE4().* = ctx.value(u8);
    }

    pub fn func_800BC1CC(ctx: *zd.Ctx, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        if (ctx.value(u1) == 1) v.g.D_800F4C22().* = ctx.value(i16);
    }

    pub fn func_800BFBB8(ctx: *zd.Ctx, p: anytype, i: anytype) c_short {
        _ = p;
        _ = i;
        if (ctx.value(u1) == 1) v.g.D_800F4C38()[ctx.value(u8) % 4] = null;
        if (ctx.value(u8) < 100) return 0;
        return ctx.value(c_short) | 1;
    }

    pub fn func_800BD610(ctx: *zd.Ctx) c_int {
        if (ctx.value(u1) == 1) v.g.D_800F4C54().* = ctx.value(i16);
        const r = ctx.value(c_int);
        if (ctx.value(u8) < 128) return (r & ~@as(c_int, 0xFF)) | 0xFF;
        return r;
    }
};
