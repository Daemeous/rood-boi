//! zig_diff spec: func_800B9564 -> func_800B9564.func_800B9564 (4A0A8.c).
//! Inputs: arg0 is four random arena bytes, so the index nibble (arg0[1] & 0xF),
//! the shift (arg0[1] >> 4) and arg0[2] are any values; arg1 is random (unused).
//! The text box is an arena object that is always valid, because the C does not
//! test vs_battle_getTextBox for NULL. vs_battle_getShort returns a halfword drawn
//! from the edges 0, 0x1FFF, 0x2000, 0x2001, -1, -32768, 32767 or any value: below
//! 0x2000 unsigned, exactly 0x2000, above it, and negative (which reads as unsigned
//! 0x8000 and up, so it takes the 0x20 path).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9564));

pub const classes = [_][:0]const u8{ "below", "equal", "above", "negative" };

/// The text box vs_battle_getTextBox hands back (set by prepare).
var box: *v.vs_battle_textBox = undefined;
/// The halfword vs_battle_getShort returns (set by prepare).
var short_result: c_short = 0;

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = ctx;
        _ = p;
        return short_result;
    }

    pub fn vs_battle_getTextBox(ctx: *zd.Ctx, index: anytype) ?*v.vs_battle_textBox {
        _ = index;
        // The callee may initialise the box; the C never reads unk4 back.
        box.unk4 = ctx.value(i16);
        return box;
    }

    pub fn func_800BFE50(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    box = ctx.alloc(v.vs_battle_textBox);
    short_result = ctx.pick(c_short, &.{ 0, 0x1FFF, 0x2000, 0x2001, -1, -32768, 32767 });
    const arg0: [*]u8 = ctx.allocBytes(4, 1);
    return .{ arg0, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    _ = args;
    if (short_result < 0) return 3;
    if (short_result == 0x2000) return 1;
    if (short_result < 0x2000) return 0;
    return 2;
}
