//! zig_diff spec: func_800D4A94 -> func_800D4A94.func_800D4A94 (5BF94.c).
//! The C is one call to func_800CFEF0(arg0) followed by `return 1`, so the
//! inputs cover the argument being null or a live arena object. The callee
//! stub may rewrite the object's header fields (unk8, unk9, unkA), which the
//! trace of the object's state compares, and it may be skipped entirely
//! when the argument is null.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D4A94));

pub const classes = [_][:0]const u8{ "null-arg", "object-arg" };

pub const stubs = struct {
    pub fn func_800CFEF0(ctx: *zd.Ctx, p: anytype) void {
        if (p == null) return;
        const a: *v.D_800F53B8_t = @ptrCast(@alignCast(p.?));
        // The real callee may update the header; the C never reads it back.
        a.unk8 = ctx.value(u8);
        a.unk9 = ctx.value(u8);
        a.unkA = ctx.value(u16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(5)) return .{null};
    const obj = ctx.alloc(v.D_800F53B8_t);
    return .{obj};
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
