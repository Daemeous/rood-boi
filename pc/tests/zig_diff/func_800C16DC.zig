//! zig_diff spec: func_800C16DC -> func_800C16DC.func_800C16DC (58578.c).
//! Inputs: the global D_800EB9B8 is set by prepare to either NULL (the C's
//! null test skips the store) or a random-filled D_800EB9B8_t in the arena
//! (the store to unk2C runs). The C makes no calls and has no ordered
//! comparisons, so the only value edge is the null test itself.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_58578");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C16DC));

pub const classes = [_][:0]const u8{ "null", "valid" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800EB9B8_t);
    v.g.D_800EB9B8().* = if (ctx.chance(3)) null else obj;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800EB9B8().* == null) 0 else 1;
}
