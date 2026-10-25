//! zig_diff spec: func_8009CC20 -> func_8009CC20.func_8009CC20 (30DB0.c).
//! Inputs: arg1 picked from the switch's labels (0, 1, 2, 4, 5), 3, -1, 6 and
//! INT_MAX, so the default arm runs too. arg0 is sized to each arm: 0..16 for
//! the actor-indexed cases (0, 4, 5), 1..5 for the slot cases (1, 2), and any
//! value for the default arm. Case 0 also gets 17, 18 and INT_MAX for the
//! early return. The 17 actor slots are arena objects, each null or with a
//! block in unk5D8, unk5D4 and unk5DC[0..3]. unk5B1, unk6E6 and unk5D4 draw
//! from small pools so the shared-sequence search finds matches and misses.
//! D_800F4528 gets random entries, null or set. func_8009D208 returns
//! negative, zero and positive values and sometimes clears a D_800F4528 slot.
//! Negative arg0 is never passed to the actor-indexed cases: the C reads
//! D_800F4538[arg0] before any test, so it would read outside the table.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009CC20));

pub const classes = [_][:0]const u8{
    "other-arg1",
    "case0-early",
    "case0-with-data",
    "case12",
    "case45",
};

/// A fresh arena block, or null one time in `null_one_in`.
fn block(ctx: *zd.Ctx, null_one_in: u32) ?*anyopaque {
    if (ctx.chance(null_one_in)) return null;
    const p: *anyopaque = @ptrCast(ctx.allocBytes(16, 16));
    return p;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg1 = ctx.edge(c_int, &.{ 0, 1, 2, 4, 5, 0, 3, -1, 6, 0x7FFF_FFFF });
    const arg0: c_int = switch (arg1) {
        0 => if (ctx.chance(4)) ctx.edge(c_int, &.{ 17, 18, 0x7FFF_FFFF }) else ctx.range(c_int, 0, 16),
        1, 2 => ctx.range(c_int, 1, 5),
        4, 5 => ctx.range(c_int, 0, 16),
        else => ctx.pick(c_int, &.{ 0, 1, 17 }),
    };

    var acts: [17]*v.D_800F4538_t = undefined;
    for (&acts) |*slot| {
        const a = ctx.alloc(v.D_800F4538_t);
        a.unk5B1 = ctx.edge(u8, &.{ 0, 0, 1, 2, 0x80 });
        a.unk6E6 = ctx.edge(u16, &.{ 0, 1, 2, 0xFFFF });
        psx.put(&a.unk5D8, block(ctx, 3));
        psx.put(&a.unk5D4, block(ctx, 2));
        for (0..4) |k| psx.put(&a.unk5DC[k], block(ctx, 2));
        slot.* = a;
    }

    const tbl = v.g.D_800F4538();
    for (acts, 0..) |a, i| tbl[i] = if (ctx.chance(4)) null else a;

    const d = v.g.D_800F4528();
    for (0..21) |i| d[i] = block(ctx, 2);

    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    const arg0 = args[0];
    switch (args[1]) {
        0 => {
            if (arg0 < 0 or arg0 >= 17) return 1;
            const actor = v.g.D_800F4538()[@intCast(arg0)] orelse return 1;
            if (psx.get(actor.unk5D8) == null) return 1;
            return 2;
        },
        1, 2 => return 3,
        4, 5 => return 4,
        else => return 0,
    }
}

pub const stubs = struct {
    pub fn func_8009D208(ctx: *zd.Ctx, arg: anytype) c_int {
        if (ctx.value(u1) == 1) v.g.D_800F4528()[@as(usize, @intCast(arg - 1))] = null;
        const r = ctx.value(i32);
        return if (ctx.value(u2) == 0) r else 0;
    }
};
