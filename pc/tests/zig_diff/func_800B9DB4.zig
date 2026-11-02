//! zig_diff spec: func_800B9DB4 -> func_800B9DB4.func_800B9DB4 (4A0A8.c).
//! Inputs: a 4-byte buffer for arg0 (bytes 1 and 2 drive the branches) and any
//! arg1 (the C never reads it). The globals D_800F4C4E (busy flag) and
//! D_800F4C2C (state) are set to pick each path: idle with no chain, idle with
//! a chain, idle with state 2 (flags only), busy with state 2, and busy with
//! other states. Idle entries use arg0[1] in 0..2 so the D_800E9B4C[arg0[1]]
//! read stays inside its 3 entries; busy entries use any byte, including the
//! sign bit. vs_battle_getStateFlag returns 0 about a quarter of the time, and
//! func_8007C0AC sometimes rewrites D_800F4C2C, which the C re-reads.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9DB4));

pub const classes = [_][:0]const u8{ "idle-no-chain", "idle-chain", "idle-flags-only", "busy-finish", "busy-check" };

pub const stubs = struct {
    pub fn vs_battle_getStateFlag(ctx: *zd.Ctx, id: anytype) u8 {
        _ = id;
        if (ctx.value(u8) < 96) return 0;
        return ctx.value(u8);
    }

    pub fn func_8007C0AC(ctx: *zd.Ctx, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        if (ctx.value(u2) == 0) v.g.D_800F4C2C().* = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(4, 1);
    const d4e = v.g.D_800F4C4E();
    const d4c = v.g.D_800F4C2C();
    const k = ctx.range(u8, 0, 4);
    const chain = ctx.pick(u8, &.{ 1, 2, 0x80, 0xFF });
    switch (k) {
        0 => {
            d4e.* = 0;
            d4c.* = ctx.int(u8);
            buf[2] = 0;
        },
        1 => {
            d4e.* = 0;
            d4c.* = ctx.edge(u8, &.{ 0, 1, 3, 0x82 });
            buf[2] = chain;
        },
        2 => {
            d4e.* = 0;
            d4c.* = 2;
            buf[2] = chain;
        },
        3 => {
            d4e.* = ctx.edge(u8, &.{ 1, 0x80 });
            d4c.* = 2;
            buf[2] = ctx.int(u8);
        },
        else => {
            d4e.* = ctx.edge(u8, &.{ 1, 0x80, 0xFF });
            d4c.* = ctx.edge(u8, &.{ 0, 1, 3, 0x82 });
            buf[2] = ctx.int(u8);
        },
    }
    // The table index is only read on the idle path: keep it in 0..2 there.
    buf[1] = if (d4e.* == 0) ctx.edge(u8, &.{ 0, 1, 2 }) else ctx.pick(u8, &.{ 0, 1, 2, 0x80, 0xFF });
    return .{ buf, ctx.int(i16) };
}

pub fn classify(args: Args) usize {
    const a = args[0];
    const busy = v.g.D_800F4C4E().* != 0;
    const c2 = v.g.D_800F4C2C().* == 2;
    if (!busy) {
        if (a[2] == 0) return 0;
        if (c2) return 2;
        return 1;
    }
    return if (c2) 3 else 4;
}
