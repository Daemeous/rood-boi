//! zig_diff spec: func_800D6508 -> func_800D6508.func_800D6508 (5BF94.c).
//! Inputs: 256 random arena entries and an index (a u8: 0, 1, 255 or any),
//! the field unk22_2 set to 1 in the indexed one; its neighbours in the
//! same bytes keep their random bits.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D6508));

pub const classes = [_][:0]const u8{ "first", "later", "last" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const entries: *v.func_800D6508_t = @ptrCast(@alignCast(ctx.allocBytes(256 * @sizeOf(v.func_800D6508_t), 16)));
    return .{ entries, ctx.pick(u8, &.{ 0, 1, 255 }) };
}

pub fn classify(args: Args) usize {
    if (args[1] == 0) return 0;
    return if (args[1] == 255) 2 else 1;
}
