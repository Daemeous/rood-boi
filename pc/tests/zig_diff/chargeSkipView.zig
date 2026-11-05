//! zig_diff spec: pc_charge_skip_view -> charge_skip.view (146C.c, charge_skip.inc).
//! Inputs: an actor index at 0..16 and just outside (-1, 17, 255, minInt), the slot empty or
//! holding an actor with any facing; the camera (scratchpad) and the spherical camera are
//! left at the harness's random bytes.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").view));

pub const classes = [_][:0]const u8{ "out-of-range", "no-actor", "actor" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const index = ctx.edge(c_int, &.{ 0, 1, 16, 16, 5, -1, 17, 255, std.math.minInt(c_int) });
    if (index >= 0 and index < 17) {
        v.g.D_800F4538().*[@intCast(index)] = if (ctx.chance(4)) null else ctx.alloc(v.D_800F4538_t);
    }
    return .{ @ptrCast(@alignCast(ctx.allocBytes(36, 16))), index };
}

pub fn classify(args: Args) usize {
    if (args[1] < 0 or args[1] >= 17) return 0;
    return if (v.g.D_800F4538().*[@intCast(args[1])] == null) 1 else 2;
}
