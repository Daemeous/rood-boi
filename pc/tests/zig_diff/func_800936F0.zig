//! zig_diff spec: func_800936F0 -> func_800936F0.func_800936F0 (2842C.c).
//! The C body returns 0 and reads neither argument, so it has no branches and
//! no value edges. The inputs are objects in the arena, or null, for each
//! argument; their bytes keep the random fill, so a mutation that reads a field
//! sees nonzero data. The four classes name the null/object combinations of
//! the two arguments, and each occurs in every run.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800936F0));

pub const classes = [_][:0]const u8{ "both-null", "arg0-only", "arg1-only", "both-objects" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const a0: ?*v.func_80092F74_t = if (ctx.chance(4)) null else ctx.alloc(v.func_80092F74_t);
    const a1: ?*v.func_80092F74_t2 = if (ctx.chance(4)) null else ctx.alloc(v.func_80092F74_t2);
    return .{ a0, a1 };
}

pub fn classify(args: Args) usize {
    const has0: usize = @intFromBool(args[0] != null);
    const has1: usize = @intFromBool(args[1] != null);
    // 0: both null, 1: only arg1, 2: only arg0, 3: both objects.
    return (has0 << 1) | has1;
}
