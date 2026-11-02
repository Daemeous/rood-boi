//! zig_diff spec: func_800BD444 -> func_800BD444.func_800BD444 (4A0A8.c).
//! Inputs: an 8-byte command buffer whose first byte is 0xE2 or 0xE3 (the
//! two switch cases), with arg0[4] zero or not (the unkA == 0 branch), and
//! arg0[3] with high nibble 0, 2 or other (the switch on arg0[3] >> 4), low
//! nibble random. The D_800F4BA4 global points at an arena block.
//! vs_battle_getShort returns a value chosen by prepare so the difference
//! from unk2 is an edge: exactly -2048, 2048, 0, 1, -1, the wrap points, and
//! random values. That covers every comparison of unk6 with its bounds and
//! with zero, in both signs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BD444));

pub const classes = [_][:0]const u8{ "E2-start", "E2-move", "E3-start", "E3-move" };

/// The value vs_battle_getShort returns this iteration (set by prepare).
var short_ret: c_short = 0;

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = ctx;
        _ = p;
        return short_ret;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const blk = ctx.alloc(v.D_800F4BA4_t);
    v.g.D_800F4BA4().* = blk;

    const buf = ctx.allocBytes(8, 2);
    buf[0] = ctx.edge(u8, &.{ 0xE2, 0xE3 });
    const high = ctx.edge(u8, &.{ 0x00, 0x20, 0x10, 0x30, 0x80, 0xF0 });
    buf[3] = high | @as(u8, ctx.int(u4));
    buf[4] = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });

    // The difference getShort - unk2 (as an i16) is the edge d.
    const d = ctx.pick(i16, &.{ -2048, 2048, -2047, 2047, -2049, 2049, 0, 1, -1, 4096, -4096, 0x7FFF, -0x8000 });
    const s = if (buf[0] == 0xE2) &blk.unk168 else &blk.unk174;
    short_ret = s.unk2 +% d;

    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const b = args[0];
    const base: usize = if (b[0] == 0xE2) 0 else 2;
    return base + (if (b[4] == 0) @as(usize, 0) else 1);
}
