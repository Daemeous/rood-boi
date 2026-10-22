//! zig_diff spec: func_800C16FC -> func_800C16FC.func_800C16FC (58578.c).
//! Inputs: two packed coordinates (y in the high half, x in the low half, both
//! signed) and a count. Most runs use small coordinates so that the flat line
//! (y0 == y1), dx == 0, the segment edges (start == dx, end == dx) and the
//! tail test all take their equal and unequal sides often. A quarter of the
//! runs use full random coordinates for wrapping, sign and high-bit cases, and
//! the count runs over -2..18 with edges 0, 15, 16, 17 and full random values.
//! vs_battle_addTile sometimes re-points vs_scratch.unk8, which the C re-reads
//! before each call. The length & 1 sign-bit flag is unreachable: length is
//! |y0 - y1| >> 1 after the negation, so it is never negative.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_58578");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C16FC));

pub const classes = [_][:0]const u8{ "flat", "count-out", "left", "odd", "even" };

fn pack(hi: i16, lo: i16) c_int {
    const u: u32 = (@as(u32, @as(u16, @bitCast(hi))) << 16) | @as(u32, @as(u16, @bitCast(lo)));
    return @bitCast(u);
}

fn hiOf(x: c_int) i32 {
    return @as(i16, @truncate(x >> 16));
}

fn loOf(x: c_int) i32 {
    return @as(i16, @truncate(x));
}

pub const stubs = struct {
    pub fn vs_battle_addTile(ctx: *zd.Ctx, before: anytype, rgb0: anytype, xy: anytype, wh: anytype) void {
        _ = before;
        _ = rgb0;
        _ = xy;
        _ = wh;
        // The real callee may move the scratch's table pointer; the C re-reads it.
        if (ctx.value(u3) == 0) {
            psx.put(&v.g.vs_scratch().unk8, ctx.allocBytes(32, 16));
        }
    }

    pub fn vs_battle_insertTpage(ctx: *zd.Ctx, arg0: anytype, arg1: anytype) void {
        _ = ctx;
        _ = arg0;
        _ = arg1;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = v.g.vs_scratch();
    psx.put(&s.unk8, ctx.allocBytes(32, 16));

    var y0: i16 = undefined;
    var y1: i16 = undefined;
    var x0: i16 = undefined;
    var x1: i16 = undefined;
    if (ctx.chance(4)) {
        // Full random: wrapping, sign and high-bit cases.
        y0 = ctx.int(i16);
        y1 = ctx.int(i16);
        x0 = ctx.int(i16);
        x1 = ctx.int(i16);
    } else {
        // Small geometry: the equal sides of every comparison come up often.
        y1 = ctx.range(i16, -24, 24);
        y0 = if (ctx.chance(4)) y1 else y1 + ctx.range(i16, -24, 24);
        x0 = ctx.range(i16, -24, 24);
        const half: i32 = @intCast(@abs(@as(i32, y0) - y1) >> 1);
        const mid: i32 = x0 + half;
        x1 = @intCast(mid + ctx.range(i32, -4, 4));
    }

    const count: c_int = if (ctx.chance(3))
        ctx.pick(c_int, &.{ -1, 0, 15, 16, 17, 255 })
    else
        ctx.range(c_int, -2, 18);

    return .{ pack(y0, x0), pack(y1, x1), count };
}

pub fn classify(args: Args) usize {
    const count = args[2];
    const y0 = hiOf(args[0]);
    const y1 = hiOf(args[1]);
    if (y0 == y1) return 0;
    if (count < 0 or count > 15) return 1;
    const d = y0 - y1;
    const half: i32 = @intCast(@abs(d) >> 1);
    const dx = loOf(args[1]) - (loOf(args[0]) + half);
    if (dx < 0) return 2;
    return if ((@abs(d) & 1) != 0) 3 else 4;
}
