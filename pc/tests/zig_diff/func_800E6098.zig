//! zig_diff spec: func_800E6098 -> func_800E6098.func_800E6098 (7CD68.c).
//! Inputs: two 32x32 u16 tables, D_800F58B8 and D_800F58D0, built in the arena
//! and pointed to by their globals. Each run picks a mode: random cells (mixed),
//! all cells active (bit 0x20 set, 0x2000 clear), all inactive (0x20 clear), or
//! all blocked (both 0x20 and 0x2000 set). func_800E5FDC returns 0, -1 or a
//! random int, so the `r >= 0` test sees equal, negative and positive values.
//! func_800E5EC0 rewrites random cells of both tables, which the scan reads later.

const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_7CD68");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E6098));

pub const classes = [_][:0]const u8{ "mixed", "all-active", "all-inactive", "all-blocked" };

/// The mode of the current run, chosen by prepare and read by classify.
var mode: usize = 0;

fn fillCells(ctx: *zd.Ctx, a: [*][32]u16, b: [*][32]u16) void {
    for (0..32) |z| for (0..32) |x| {
        var va = ctx.int(u16);
        var vb = ctx.int(u16);
        switch (mode) {
            1 => {
                va |= 0x20;
                vb &= ~@as(u16, 0x2000);
            },
            2 => {
                va &= ~@as(u16, 0x20);
            },
            3 => {
                va |= 0x20;
                vb |= 0x2000;
            },
            else => {},
        }
        a[z][x] = va;
        b[z][x] = vb;
    };
}

pub fn prepare(ctx: *zd.Ctx) Args {
    mode = ctx.range(usize, 0, 3);
    const a: [*][32]u16 = @ptrCast(@alignCast(ctx.allocBytes(2048, 16)));
    const b: [*][32]u16 = @ptrCast(@alignCast(ctx.allocBytes(2048, 16)));
    fillCells(ctx, a, b);
    v.g.D_800F58B8().* = a;
    v.g.D_800F58D0().* = b;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return mode;
}

pub const stubs = struct {
    /// Returns 0 or -1 about a quarter of the time each, else a random int.
    pub fn func_800E5FDC(ctx: *zd.Ctx, x: anytype, z: anytype) c_int {
        _ = x;
        _ = z;
        const k = ctx.value(u8);
        if (k < 48) return 0;
        if (k < 96) return -1;
        return ctx.value(c_int);
    }

    /// Sometimes rewrites a random cell of each table, which the C reads later.
    pub fn func_800E5EC0(ctx: *zd.Ctx, x: anytype, z: anytype, r: anytype) void {
        _ = x;
        _ = z;
        _ = r;
        if (ctx.value(u4) < 4) {
            const tb = v.g.D_800F58B8().*.?;
            tb[ctx.value(u5)][ctx.value(u5)] = ctx.value(u16);
        }
        if (ctx.value(u4) < 4) {
            const tb = v.g.D_800F58D0().*.?;
            tb[ctx.value(u5)][ctx.value(u5)] = ctx.value(u16);
        }
    }
};
