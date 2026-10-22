//! zig_diff spec: func_800D78B8 -> func_800D78B8.func_800D78B8 (6F014.c).
//! The C copies the pointer global D_800F569C into D_800F5698 and has no
//! branches and no calls. Inputs: D_800F569C is null or a fresh arena object
//! (its bytes random). D_800F5698 keeps its random prior value, so a store
//! that is skipped, reversed or replaced is visible in the compared state.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D78B8));

pub const classes = [_][:0]const u8{ "null-source", "object-source" };

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(4)) {
        v.g.D_800F569C().* = null;
    } else {
        v.g.D_800F569C().* = ctx.alloc(v.D_800F569C_t);
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F569C().* == null) 0 else 1;
}
