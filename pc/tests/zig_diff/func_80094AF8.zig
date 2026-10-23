//! zig_diff spec: func_80094AF8 -> func_80094AF8.func_80094AF8 (2842C.c).
//! The function has no callees and no branches: it clears unkB and decrements
//! unk6. Inputs: a random D_800F1BAC_t in the arena, with unk6 drawn from
//! edges (0, 1, -1, 32767, -32768) half the time and any i16 otherwise, so the
//! decrement hits zero, the signed minimum that wraps to 32767, and ordinary
//! values. unkB and the other bytes keep the random fill, so a dropped or
//! misplaced store shows.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80094AF8));

pub const classes = [_][:0]const u8{ "unk6-zero", "unk6-min", "other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F1BAC_t);
    obj.unk6 = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    return .{obj};
}

pub fn classify(args: Args) usize {
    const obj = args[0].?;
    if (obj.unk6 == 0) return 0;
    if (obj.unk6 == -32768) return 1;
    return 2;
}
