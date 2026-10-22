//! zig_diff spec: func_800C1A40 -> func_800C1A40.func_800C1A40 (58578.c).
//! Inputs: two packed (y, x) words and a count (mostly -2..33, a quarter of the
//! time any c_int). The x1 word sits near the midline, so dx is 0, small or large
//! with either sign; y0 equals y1 one time in twenty (length 0, the flat lines and
//! the `y1 != ...` test at i = 15); the x0 word covers the sign bit of its low half.
//! vs_scratch.unk8 is set to an arena block. The tile and Tpage calls are
//! recording stubs: the C never reads what they write (the ordering table), so
//! they change nothing it reads back. `length` is an absolute difference of two
//! 16-bit values, so its sign bit is never set; that half of the zero-test
//! value flag cannot be reached.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_58578");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C1A40));

pub const classes = [_][:0]const u8{ "count-clipped", "flat", "sloped" };

pub const stubs = struct {
    // Both callees write the ordering table, which the C never reads back:
    // nothing else changes, and the arguments are recorded.
    pub fn vs_battle_addTile(ctx: *zd.Ctx, before: anytype, rgb0: anytype, xy: anytype, wh: anytype) void {
        _ = ctx;
        _ = before;
        _ = rgb0;
        _ = xy;
        _ = wh;
    }
    pub fn vs_battle_insertTpage(ctx: *zd.Ctx, arg0: anytype, arg1: anytype) void {
        _ = ctx;
        _ = arg0;
        _ = arg1;
    }
};

fn pack(y: i16, x: u16) c_int {
    const hi: u32 = @as(u16, @bitCast(y));
    return @bitCast((hi << 16) | @as(u32, x));
}

fn clip16(x: i32) i16 {
    return @intCast(std.math.clamp(x, -32768, 32767));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch = v.g.vs_scratch();
    psx.put(&scratch.unk8, ctx.alloc(u64));

    const y1: i16 = if (ctx.chance(4))
        ctx.pick(i16, &.{ 0, -1, 32767, -32768 })
    else
        @intCast(ctx.range(i32, -64, 64));
    var y0: i16 = clip16(@as(i32, y1) + ctx.range(i32, -40, 40));
    if (ctx.chance(5)) y0 = y1;

    const L: i32 = @intCast(@abs(@as(i32, y0) - @as(i32, y1)));
    const half: i32 = L >> 1;

    const x0: u16 = ctx.pick(u16, &.{ 0, 1, 0x7FFF, 0x8000, 0xFFFF });
    const x0s: i16 = @bitCast(x0);
    const midX: i32 = @as(i32, x0s) - half;
    const delta: i32 = switch (ctx.range(u8, 0, 5)) {
        0, 1 => 0,
        2 => 1,
        3 => -1,
        4 => ctx.range(i32, -20, 20),
        else => ctx.range(i32, -300, 300),
    };
    const midBits: u32 = @bitCast(midX + delta);
    const x1: u16 = @truncate(midBits);

    const count: c_int = if (ctx.chance(4)) ctx.int(c_int) else ctx.range(c_int, -2, 33);

    return .{ pack(y0, x0), pack(y1, x1), count };
}

pub fn classify(args: Args) usize {
    const count = args[2];
    if (count < 0 or count > 31) return 0;
    if ((args[0] >> 16) == (args[1] >> 16)) return 1;
    return 2;
}
