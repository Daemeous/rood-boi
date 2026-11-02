//! zig_diff spec: func_800D6538 -> func_800D6538.func_800D6538 (5BF94.c).
//! Inputs: a script whose unkD1C.unk30 is an arena block with random flags
//! (bit 0 set or clear).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D6538));

pub const classes = [_][:0]const u8{ "bit-clear", "bit-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.D_800F53B8_t);
    psx.put(&s.unkD1C.unk30, ctx.alloc(v.D_800F53B8_t4));
    return .{s};
}

pub fn classify(args: Args) usize {
    return psx.get(args[0].?.unkD1C.unk30).?.unk1 & 1;
}
