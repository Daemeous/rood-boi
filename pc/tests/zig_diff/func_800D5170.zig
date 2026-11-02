//! zig_diff spec: func_800D5170 -> func_800D5170.func_800D5170 (5BF94.c).
//! Inputs: a script whose byte pointer unkC is a 64 KiB arena buffer (the
//! u16 cursor reaches all of it), the cursor at 0, 1, 0xFFFF (it wraps to 0)
//! or anywhere.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D5170));

pub const classes = [_][:0]const u8{ "start", "middle", "wraps" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.D_800F53B8_t);
    psx.put(&s.unkC, @as(*u8, @ptrCast(ctx.allocBytes(0x10000, 16))));
    s.unkA = ctx.pick(u16, &.{ 0, 1, 0xFFFF, 0xFFFE, 0x8000 });
    return .{s};
}

pub fn classify(args: Args) usize {
    const a = args[0].?.unkA;
    if (a == 0) return 0;
    return if (a == 0xFFFF) 2 else 1;
}
