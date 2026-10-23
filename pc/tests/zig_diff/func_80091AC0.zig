//! zig_diff spec: func_80091AC0 -> func_80091AC0.func_80091AC0 (2842C.c).
//! Inputs: any arg0 (0, -1, 1, INT_MAX, INT_MIN and random values). The callee
//! func_8009195C returns NULL about a third of the time, otherwise a
//! prepared arena int whose bit 0x1000 is set or clear (from edge values and
//! random bytes). The callee sometimes overwrites that int before returning,
//! so the OR reads back the callee's value.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80091AC0));

pub const classes = [_][:0]const u8{ "null-return", "object-bit-clear", "object-bit-set" };

/// Set by prepare: the callee returns NULL this iteration.
var want_null: bool = false;
/// Set by prepare: the object the callee returns when not NULL.
var obj: ?*c_int = null;

pub const stubs = struct {
    pub fn func_8009195C(ctx: *zd.Ctx, arg0: anytype) ?*c_int {
        _ = arg0;
        if (want_null) return null;
        // The callee may change the object it hands back before the C ORs it.
        if (ctx.value(u1) == 1) obj.?.* = ctx.value(c_int);
        return obj;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    want_null = ctx.chance(3);
    const o = ctx.alloc(c_int);
    o.* = ctx.pick(c_int, &.{ 0, 0x1000, -1, 0x7FFFFFFF, -4096 });
    obj = o;
    const arg = ctx.pick(c_int, &.{ 0, -1, 1, 0x7FFFFFFF, -2147483648 });
    return .{arg};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (want_null) return 0;
    const bit_set = (obj.?.* & 0x1000) != 0;
    return if (bit_set) 2 else 1;
}
