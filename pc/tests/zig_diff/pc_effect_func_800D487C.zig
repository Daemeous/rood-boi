//! zig_diff spec: pc_effect_func_800D487C -> effect_callbacks.pc_effect_func_800D487C (5BF94.c).
//! The C is `return func_800D487C(node);`. Inputs: a null node or an arena block; the func_800D487C stub returns any int and the call (argument, state hash) is compared.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").pc_effect_func_800D487C));

pub const classes = [_][:0]const u8{ "null-node", "block-node" };

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(3)) return .{null};
    return .{@ptrCast(ctx.allocBytes(64, 16))};
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}

pub const stubs = struct {
    pub fn func_800D487C(ctx: *zd.Ctx, a0: anytype) c_int {
        _ = a0;
        return ctx.value(c_int);
    }
};
