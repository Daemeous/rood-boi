//! zig_diff spec: func_800D78CC -> func_800D78CC.func_800D78CC (6F014.c).
//! The C copies one pointer global into another and never dereferences either.
//! Inputs: the source global is random bytes, and prepare sets it to null, to a
//! fresh arena object, or leaves it random. The destination is sometimes preset
//! to the source value, so a dropped store is caught only where it differs.
//! The C has no branches and no calls, so there are no waivers.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D78CC));

pub const classes = [_][:0]const u8{ "src-null", "src-arena", "src-random" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const src = v.g.D_800F5698();
    const dst = v.g.D_800F569C();
    if (ctx.chance(4)) {
        src.* = null;
    } else if (ctx.chance(2)) {
        src.* = ctx.alloc(v.D_800F569C_t);
    }
    // Sometimes the destination already holds the source: the store is then invisible.
    if (ctx.chance(4)) dst.* = src.*;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const src = v.g.D_800F5698();
    if (src.* == null) return 0;
    const arena_start = @intFromPtr(&zd.zdiff_arena[0]);
    const arena_end = arena_start + zd.arena_size;
    const p = @intFromPtr(src.*.?);
    if (p >= arena_start and p < arena_end) return 1;
    return 2;
}
