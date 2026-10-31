//! zig_diff spec: func_800D4D98 -> func_800D4D98.func_800D4D98 (5BF94.c).
//! The C ignores its argument (`__attribute__((unused))`), clears the byte
//! global D_800F522C and returns 1. The inputs cover a null argument and a
//! valid arena object: the C must not read either, so both runs see the same
//! state and return value. The global keeps its random fill before each
//! call, so a dropped, moved or wrong-valued store shows up in the state
//! comparison.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D4D98));

pub const classes = [_][:0]const u8{ "null-arg", "object-arg" };

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(4)) return .{null};
    const obj = ctx.alloc(v.D_800F53B8_t);
    return .{obj};
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
