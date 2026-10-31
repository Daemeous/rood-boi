//! zig_diff spec: func_800D48E4 -> func_800D48E4.func_800D48E4 (5BF94.c).
//! Inputs: arg0 is a random D_800F53B8_t in the arena or null (the C only
//! passes it on), and the global D_800F522C takes 0, 1, 0x7F, 0x80, 0xFF or a
//! random byte, so both the zero and nonzero tests see values with and
//! without the sign bit. The callee func_800D46DC is a recording stub that
//! writes arg0's unk8 (when non-null) and sometimes D_800F522C, as the real
//! callee may.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D48E4));

pub const classes = [_][:0]const u8{ "null-arg", "flag-zero", "flag-nonzero" };

pub const stubs = struct {
    pub fn func_800D46DC(ctx: *zd.Ctx, flag: anytype, obj: anytype) void {
        _ = flag;
        if (obj) |p| {
            const a: *v.D_800F53B8_t = @ptrCast(@alignCast(p));
            a.unk8 = ctx.value(u8);
        }
        if (ctx.value(u1) == 1) v.g.D_800F522C().* = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F53B8_t);
    v.g.D_800F522C().* = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    return .{if (ctx.chance(4)) null else obj};
}

pub fn classify(args: Args) usize {
    if (args[0] == null) return 0;
    return if (v.g.D_800F522C().* == 0) 1 else 2;
}
