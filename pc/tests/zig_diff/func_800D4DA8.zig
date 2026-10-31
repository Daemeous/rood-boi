//! zig_diff spec: func_800D4DA8 -> func_800D4DA8.func_800D4DA8 (5BF94.c).
//! The C ignores its argument and returns 1, so the inputs only vary the
//! pointer's nullness: a null argument (never dereferenced) and a valid arena
//! object. The return value must be 1 in both cases. Nothing else is read or
//! written, so the globals and scratchpad only need to stay untouched.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D4DA8));

pub const classes = [_][:0]const u8{ "null", "object" };

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(2)) return .{null};
    return .{ctx.alloc(v.D_800F53B8_t)};
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
