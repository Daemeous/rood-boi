//! zig_diff spec: func_800D4BD0 -> func_800D4BD0.func_800D4BD0 (5BF94.c).
//! Inputs: a random script; func_800D5170 (the stub) returns the register
//! index 0..3, or 4..255 (past unk10, into the rest of the script: the C
//! does not check); func_800D2ADC is a recording stub.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D4BD0));

/// The byte func_800D5170 returns in this iteration (set by prepare).
var chosen: u8 = 0;

pub const stubs = struct {
    pub fn func_800D5170(ctx: *zd.Ctx, p: anytype) u8 {
        _ = ctx;
        _ = p;
        return chosen;
    }
};

pub const classes = [_][:0]const u8{ "in-unk10", "past-unk10" };

pub fn prepare(ctx: *zd.Ctx) Args {
    chosen = ctx.pick(u8, &.{ 0, 3, 4, 255 });
    return .{ctx.alloc(v.D_800F53B8_t)};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (chosen < 4) 0 else 1;
}
