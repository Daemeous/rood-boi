//! zig_diff spec: _renderTimer -> render_timer.renderTimer (5BF94.c).
//! Inputs: the timer word with its top byte zero or not (59:59:99), the
//! state flags' hundredths random; D_800EC258 an arena text buffer;
//! vs_battle_toBCD returns any int (its nibbles become digits);
//! vs_battle_renderTextRaw is a stub (the text pointer and the OT minus 4).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").renderTimer));

pub const classes = [_][:0]const u8{ "running", "capped" };

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800EC258().* = @ptrCast(ctx.allocBytes(16, 16));
    psx.put(&v.g.vs_scratch().unk4, @as(*anyopaque, @ptrCast(ctx.allocBytes(16, 16) + 8)));
    const value: *c_int = @ptrCast(@alignCast(ctx.allocBytes(4, 16)));
    if (ctx.random.boolean()) value.* &= 0xFFFFFF;
    return .{value};
}

pub fn classify(args: Args) usize {
    return if (@as(u32, @bitCast(args[0].*)) >> 24 != 0) 1 else 0;
}
