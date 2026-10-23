//! zig_diff spec: func_800A9E38 -> func_800A9E38.func_800A9E38 (40564.c).
//! Inputs: arg0 is a table index 0..16 (the ends included). Every slot of
//! D_800F4538 holds one of two actors chosen at random, so any index the C
//! reads is a valid actor, and an index taken modulo 8 names a slot that may
//! hold the other actor. arg2 is the sentinel -1 (the early return), a
//! u16-range store, or a value that wraps when stored to unk5C0 (0x10000, -2,
//! INT_MIN and friends). arg1 is any short. The callee's stub rewrites the
//! table entry (to the second actor) and the stored halfword, so the restore
//! must use the actor the C captured, not a fresh table lookup.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A9E38));

pub const classes = [_][:0]const u8{ "sentinel", "store", "store-wide" };

/// The second actor the callee may install in the table.
var other: ?*v.D_800F4538_t = null;

pub const stubs = struct {
    pub fn func_800AA984(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) void {
        _ = b;
        _ = c;
        const idx: usize = @intCast(a);
        const slot = &v.g.D_800F4538()[idx];
        switch (ctx.value(u2)) {
            0 => slot.* = other,
            1 => if (slot.*) |p| {
                p.unk5C0 = ctx.value(u16);
            },
            else => {},
        }
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    other = ctx.alloc(v.D_800F4538_t);
    actor.unk5C0 = ctx.int(u16);

    // Every slot is a valid actor, one of the two chosen at random, so the
    // masked index of a mutation reads a different actor than the C does.
    for (v.g.D_800F4538()) |*slot| {
        slot.* = if (ctx.chance(2)) actor else other;
    }

    const arg0: c_int = if (ctx.chance(2))
        ctx.edge(c_int, &.{ 0, 16 })
    else
        ctx.range(c_int, 0, 16);

    const arg1: c_short = ctx.int(c_short);
    const arg2: c_int = ctx.pick(c_int, &.{ -1, -1, 0, 1, 0xFFFF, 0x10000, -2, 32767, -32768 });
    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    if (args[2] == -1) return 0;
    if (args[2] < 0 or args[2] > 65535) return 2;
    return 1;
}
