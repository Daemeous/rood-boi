//! zig_diff spec: func_8009E070 -> func_8009E070.func_8009E070 (30DB0.c).
//! Inputs: D_800F4538[arg0] for arg0 in 0..16 is an arena object (its table
//! entry set by prepare); its flag unk5AC_3 and the selectors unkA_0 (0,3,5,7)
//! and unkA_3 (0,2,3) are drawn from edges so case 6 takes the early return,
//! the flag-set path and the plain path. arg2 is an edge set covering 1-4, 5,
//! 6, 7, 0, 8, -1 and INT_MAX. arg1 is null or a fresh SVECTOR, which the C
//! only passes on. The callee func_8009E180 returns any c_int and may change
//! the object's unkA_0 (the C never reads that back, but the state compare
//! sees it).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009E070));

pub const classes = [_][:0]const u8{
    "other-selector",
    "case-1-4",
    "case-5",
    "case-7",
    "case-6-early-return",
    "case-6-flag-set",
    "case-6-plain",
};

pub const stubs = struct {
    pub fn func_8009E180(ctx: *zd.Ctx, p: anytype, arg1: anytype) c_int {
        _ = arg1;
        const t: *v.D_800F4538_t = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) psx.setBits(&t.unk0, "unkA_0", ctx.value(u3));
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    const idx = ctx.range(c_int, 0, 16);
    v.g.D_800F4538()[@intCast(idx)] = obj;

    psx.setBits(obj, "unk5AC_3", ctx.edge(u1, &.{ 0, 1 }));
    psx.setBits(&obj.unk0, "unkA_0", ctx.edge(u3, &.{ 0, 3, 5, 7 }));
    psx.setBits(&obj.unk0, "unkA_3", ctx.edge(u2, &.{ 0, 2, 3 }));

    const sel = ctx.edge(c_int, &.{ 1, 2, 3, 4, 5, 6, 6, 6, 7, 0, 8, -1, 0x7FFF_FFFF });

    const p: ?*v.SVECTOR = if (ctx.chance(4)) null else ctx.alloc(v.SVECTOR);
    return .{ idx, p, sel };
}

pub fn classify(args: Args) usize {
    switch (args[2]) {
        1, 2, 3, 4 => return 1,
        5 => return 2,
        7 => return 3,
        6 => {},
        else => return 0,
    }
    const obj = v.g.D_800F4538()[@intCast(args[0])].?;
    if (psx.getBits(&obj.unk0, "unkA_0") == 3 or psx.getBits(&obj.unk0, "unkA_3") == 2) return 4;
    if (psx.getBits(obj, "unk5AC_3") != 0) return 5;
    return 6;
}
