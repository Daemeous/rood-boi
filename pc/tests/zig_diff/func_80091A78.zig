//! zig_diff spec: func_80091A78 -> func_80091A78.func_80091A78 (2842C.c).
//! Inputs: arg0 is 0, 1 or -1 and arg1 is picked from edges (0, +-1, 0x7FFF,
//! 0x8000, 0xFFFF, 0x10000, -0x8000) or any int. func_8009195C either returns
//! null (a quarter of the runs) or a 4-aligned arena block whose halfword [1]
//! starts at an edge value and may be rewritten by the stub. The sum
//! element + arg1 lands outside 0..65535 in some runs (the u_short wrap) and
//! inside it in others.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
const c = @import("c_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80091A78));

pub const classes = [_][:0]const u8{ "null", "found-wrap", "found-no-wrap" };

/// Set by prepare: whether func_8009195C returns null in this run.
var found: bool = true;
/// The arena block func_8009195C hands out when it finds the object.
var block: [*]u8 = undefined;

pub const stubs = struct {
    pub fn func_8009195C(ctx: *zd.Ctx, arg0: c_int) ?*c.u_short {
        _ = arg0;
        if (!found) return null;
        // The callee may rewrite the halfword the C reads back.
        if (ctx.value(u1) == 1) {
            const p: *align(1) u16 = @ptrCast(block + 2);
            p.* = ctx.value(u16);
        }
        return @ptrCast(@alignCast(block));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    found = !ctx.chance(4);
    block = ctx.allocBytes(8, 4);
    const p: *align(1) u16 = @ptrCast(block + 2);
    p.* = ctx.pick(u16, &.{ 0, 1, 0x7FFF, 0x8000, 0xFFFF, 0xFFFE });
    const arg0 = ctx.pick(c_int, &.{ 0, 1, -1 });
    const arg1 = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFF, 0x8000, 0xFFFF, 0x10000, -0x8000 });
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    if (!found) return 0;
    const p: *align(1) u16 = @ptrCast(block + 2);
    const sum = @as(i64, p.*) + @as(i64, args[1]);
    return if (sum < 0 or sum > 0xFFFF) 1 else 2;
}
