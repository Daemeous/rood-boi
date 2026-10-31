//! zig_diff spec: func_800D4D44 -> func_800D4D44.func_800D4D44 (5BF94.c).
//! The C ignores its argument and calls func_800D78F0 once, then returns 1.
//! Inputs: a null argument and a pointer to a random D_800F53B8_t in the
//! arena. The argument is never read, so both must give the same call
//! and return value. func_800D78F0 is a void recording stub; the C reads
//! nothing back from it, so it writes nothing.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D4D44));

pub const classes = [_][:0]const u8{ "null-arg", "valid-arg" };

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(2)) return .{null};
    const block = ctx.alloc(v.D_800F53B8_t);
    return .{block};
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}

pub const stubs = struct {
    // Void: nothing the C reads back changes.
    pub fn func_800D78F0(ctx: *zd.Ctx) void {
        _ = ctx;
    }
};
