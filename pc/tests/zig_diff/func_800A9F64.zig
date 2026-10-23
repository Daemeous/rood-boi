//! zig_diff spec: func_800A9F64 -> func_800A9F64.func_800A9F64 (40564.c).
//! Inputs: an index 0-16 into the D_800F4538 pointer table, each entry a fresh
//! arena D_800F4538_t (or null on the -1 path, which never dereferences it);
//! an optional SVECTOR; and arg2 from edges (-1, 0, 1, -2, 255, 256, 0x10001,
//! the i32 extremes). The callee func_800AAA88 is a recording stub that
//! sometimes rewrites the object's unk5C0, which the C's restore must overwrite.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A9F64));

pub const classes = [_][:0]const u8{ "minus-one", "minus-one-null-entry", "store-restore" };

pub const stubs = struct {
    pub fn func_800AAA88(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype) void {
        _ = arg1;
        _ = arg2;
        // The callee may change the object the C stored into; the C then restores it.
        const obj = v.g.D_800F4538()[@intCast(arg0)] orelse return;
        if (ctx.value(u1) == 1) obj.unk5C0 = ctx.value(u16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const idx = ctx.range(c_int, 0, 16);
    const arg2 = ctx.pick(c_int, &.{ -1, -1, 0, 1, -2, 255, 256, 0x10001, 0x7FFF_FFFF, -2147483648 });
    const obj = ctx.alloc(v.D_800F4538_t);
    const table = v.g.D_800F4538();
    table[@intCast(idx)] = obj;
    // On the -1 path the C never reads the entry: a null entry must not fault.
    if (arg2 == -1 and ctx.chance(4)) table[@intCast(idx)] = null;
    const arg1: ?*v.SVECTOR = if (ctx.chance(4)) null else ctx.alloc(v.SVECTOR);
    return .{ idx, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    if (args[2] != -1) return 2;
    return if (v.g.D_800F4538()[@intCast(args[0])] == null) 1 else 0;
}
