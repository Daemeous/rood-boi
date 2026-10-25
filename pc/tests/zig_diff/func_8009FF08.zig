//! zig_diff spec: func_8009FF08 -> func_8009FF08.func_8009FF08 (30DB0.c).
//! Inputs: the global table D_800F4538 (17 entries, each null, one of two
//! arena objects) and an index into it that always names a non-null object.
//! That object's previous-owner byte unk12 is 0xFF or 0..16, so the C only
//! indexes the table in range. arg1 is -1 about a quarter of the time and
//! otherwise an edge or random value, and arg2/arg3 are random. The C calls
//! nothing, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009FF08));

pub const classes = [_][:0]const u8{ "arg1-minus-one", "prev-0xFF", "prev-null", "prev-self", "prev-other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const objA = ctx.alloc(v.D_800F4538_t);
    const objB = ctx.alloc(v.D_800F4538_t);
    const tbl = v.g.D_800F4538();
    for (tbl) |*slot| {
        slot.* = switch (ctx.range(u8, 0, 9)) {
            0...2 => null,
            3...5 => objA,
            else => objB,
        };
    }
    const idx = ctx.range(c_int, 0, 16);
    const me = if (ctx.chance(2)) objA else objB;
    tbl[@intCast(idx)] = me;
    me.unk0.unk12 = if (ctx.chance(3)) 0xFF else ctx.range(u8, 0, 16);
    const arg1: c_int = if (ctx.chance(4)) -1 else ctx.pick(c_int, &.{ 0, 1, -2, 255, 256, 0x7FFF_FFFF, -2147483648 });
    return .{ idx, arg1, ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    if (args[1] == -1) return 0;
    const tbl = v.g.D_800F4538();
    const me = tbl[@intCast(args[0])].?;
    if (me.unk0.unk12 == 0xFF) return 1;
    const other = tbl[me.unk0.unk12] orelse return 2;
    return if (other == me) 3 else 4;
}
