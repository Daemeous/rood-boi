//! zig_diff spec: func_800C56C0 -> func_800C56C0.func_800C56C0 (5BF94.c).
//! Inputs: D_800EB9B8 points at an arena object. The two loop counts
//! (D_800F4CB1, D_800F4CB0) are each 0 or 1..32, so both loops are skipped,
//! run alone, or run together. The 2x16 index table D_800F4C70 holds edge
//! indices: in range, 24 and 255 (past unk990's 24 entries, but inside the
//! arena the object lives in), and 31 (the last table entry the counts can
//! reach). The callee func_800C51B4 may rewrite both counts (kept at 0..31 so
//! the loops stay inside D_800F4C70) and one table entry, which the C must
//! read after the call.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C56C0));

pub const stubs = struct {
    pub fn func_800C51B4(ctx: *zd.Ctx, n: anytype) void {
        _ = n;
        if (ctx.value(u1) == 1) v.g.D_800F4CB0().* = ctx.value(u8) & 31;
        if (ctx.value(u1) == 1) v.g.D_800F4CB1().* = ctx.value(u8) & 31;
        if (ctx.value(u1) == 1) {
            const row = ctx.value(u1);
            const col = ctx.value(u8) & 15;
            const val = ctx.value(u8);
            v.g.D_800F4C70()[row][col] = val;
        }
    }
};

pub const classes = [_][:0]const u8{ "no-loops", "first-loop-only", "second-loop-only", "both-loops" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.func_800C56C0_t);
    v.g.D_800EB9B8().* = obj;
    v.g.D_800F4CB0().* = ctx.edge(u8, &.{ 0, 1, 2, 15, 16, 17, 31, 32 });
    v.g.D_800F4CB1().* = ctx.edge(u8, &.{ 0, 1, 2, 15, 16, 17, 31, 32 });
    const table = v.g.D_800F4C70();
    for (table) |*row| {
        for (row) |*e| e.* = ctx.pick(u8, &.{ 0, 1, 15, 16, 23, 24, 31, 255 });
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const first = v.g.D_800F4CB1().* != 0;
    const second = v.g.D_800F4CB0().* != 0;
    if (!first and !second) return 0;
    if (first and !second) return 1;
    if (!first and second) return 2;
    return 3;
}
